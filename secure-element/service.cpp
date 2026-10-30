// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#include "RadioCallbacks.h"
#include <aidl/android/hardware/secure_element/BnSecureElement.h>
#include <aidl/android/hardware/secure_element/ISecureElementCallback.h>
#include <aidl/android/hardware/radio/sim/IRadioSim.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_ibinder.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace se = aidl::android::hardware::secure_element;
namespace radio = aidl::android::hardware::radio;
namespace sim = radio::sim;
using Status = ndk::ScopedAStatus;
using Bytes = std::vector<uint8_t>;

static Status error(int code = se::ISecureElement::IOERROR) {
    return Status::fromServiceSpecificError(code);
}
static std::string hex(const Bytes& bytes) {
    std::string out;
    for (uint8_t b : bytes) {
        out += "0123456789ABCDEF"[b >> 4];
        out += "0123456789ABCDEF"[b & 15];
    }
    return out;
}
static bool unhex(const std::string& text, Bytes* out) {
    out->clear();
    if (text.size() % 2) return false;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size(); i += 2) {
        int a = digit(text[i]), b = digit(text[i + 1]);
        if (a < 0 || b < 0) { out->clear(); return false; }
        out->push_back((a << 4) | b);
    }
    return true;
}

class SecureElement final : public se::BnSecureElement {
    struct Reply {
        radio::RadioResponseInfo info;
        int value = 0;
        Bytes bytes;
        std::string text;
        int sw1 = 0, sw2 = 0;
    };
    std::string backendName;
    std::mutex operation, responses;
    std::condition_variable completed;
    std::shared_ptr<sim::IRadioSim> backend;
    std::shared_ptr<RadioSimResponse> response;
    std::shared_ptr<RadioSimIndication> indication;
    std::shared_ptr<se::ISecureElementCallback> client;
    std::map<int, uid_t> channels;
    uid_t owner = static_cast<uid_t>(-1);
    int serial = 0, pending = 0;
    bool readyReply = false, connected = false;
    Reply result;
    std::atomic<bool> dirty{true}, invalidated{false};
    std::mutex notifications;
    std::condition_variable notificationReady;
    struct Event { std::shared_ptr<se::ISecureElementCallback> cb; bool state; std::string reason; };
    std::deque<Event> events;
    std::atomic<uint64_t> generation{0};
    uint64_t activeGeneration = 0;

    bool owned() const {
        return client && owner == AIBinder_getCallingUid() &&
               AIBinder_isAlive(client->asBinder().get());
    }
    void notify(bool state, const std::string& reason) {
        connected = state;
        if (client) {
            std::lock_guard lock(notifications);
            events.push_back({client, state, reason});
            notificationReady.notify_one();
        }
    }
    void discard() {
        backend.reset();
        channels.clear();
        ++generation;
        activeGeneration = generation;
        notify(false, "Radio connection lost");
    }
    bool connect() {
        if (invalidated.exchange(false) && backend) discard();
        if (backend && AIBinder_isAlive(backend->asBinder().get())) return true;
        if (backend) discard();
        ndk::SpAIBinder binder(AServiceManager_checkService(backendName.c_str()));
        if (!binder.get()) return false;
        auto candidate = sim::IRadioSim::fromBinder(binder);
        if (!candidate) return false;
        auto r = ndk::SharedRefBase::make<RadioSimResponse>();
        auto i = ndk::SharedRefBase::make<RadioSimIndication>();
        uint64_t epoch = ++generation;
        r->reply = [this, epoch](const radio::RadioResponseInfo& info, int value,
                                const Bytes& bytes, const std::string& text, int sw1, int sw2) {
            if (info.type == radio::RadioResponseType::SOLICITED_ACK) return;
            std::lock_guard lock(responses);
            if (epoch != generation || info.serial != pending) return;
            result = {info, value, bytes, text, sw1, sw2};
            readyReply = true;
            completed.notify_all();
        };
        i->acknowledge = [candidate] { candidate->responseAcknowledgement(); };
        i->changed = [this, epoch] { if (epoch == generation) { dirty = true; invalidated = true; } };
        if (!candidate->setResponseFunctions(r, i).isOk()) return false;
        backend = candidate;
        response = r;
        indication = i;
        activeGeneration = epoch;
        dirty = true;
        return true;
    }
    template<class F> bool request(F fn, Reply* reply, bool allowStateChange = false) {
        if (!connect()) return false;
        uint64_t epoch = generation;
        {
            std::lock_guard lock(responses);
            if (serial == INT32_MAX) { discard(); return false; }
            pending = ++serial;
            readyReply = false;
        }
        if (!fn(pending).isOk()) { discard(); return false; }
        std::unique_lock lock(responses);
        bool done = completed.wait_for(lock, std::chrono::seconds(5), [this] { return readyReply; });
        if (!done) { lock.unlock(); discard(); return false; }
        *reply = result;
        lock.unlock();
        if (epoch != generation || (invalidated && !allowStateChange)) return false;
        if (reply->info.type == radio::RadioResponseType::SOLICITED_ACK_EXP)
            backend->responseAcknowledgement();
        return true;
    }
    static bool success(const Reply& r) { return r.info.error == radio::RadioError::NONE; }
    Status mapped(const Reply& r) {
        if (success(r)) return Status::ok();
        if (r.info.error == radio::RadioError::MISSING_RESOURCE)
            return error(se::ISecureElement::CHANNEL_NOT_AVAILABLE);
        if (r.info.error == radio::RadioError::NO_SUCH_ELEMENT)
            return error(se::ISecureElement::NO_SUCH_ELEMENT_ERROR);
        if (r.info.error == radio::RadioError::REQUEST_NOT_SUPPORTED)
            return error(se::ISecureElement::UNSUPPORTED_OPERATION);
        return error();
    }
    bool card(Reply* r) {
        return request([this](int s) { return backend->getIccCardStatus(s); }, r) && success(*r);
    }
    void closeAll() {
        if (!connect()) { channels.clear(); return; }
        auto old = channels;
        for (const auto& entry : old) {
            int channel = entry.first;
            if (channel == 0) continue;
            Reply r;
            request([this, channel](int s) { return backend->iccCloseLogicalChannel(s, channel); }, &r);
        }
        channels.clear();
    }
    Status apdu(const Bytes& data, Bytes* out) {
        if (data.size() < 4 || data.size() > 261) return error(se::ISecureElement::UNSUPPORTED_OPERATION);
        int channel = (data[0] & 0x40) ? 4 + (data[0] & 15) : data[0] & 3;
        if (!connect()) return error(se::ISecureElement::CHANNEL_NOT_AVAILABLE);
        auto it = channels.find(channel);
        if (it == channels.end() || it->second != owner || (generation != activeGeneration || invalidated))
            return error(se::ISecureElement::CHANNEL_NOT_AVAILABLE);
        if (data[1] == 0x70) return error(se::ISecureElement::UNSUPPORTED_OPERATION);
        sim::SimApdu command;
        command.sessionId = channel;
        command.cla = data[0];
        command.instruction = data[1];
        command.p1 = data[2];
        command.p2 = data[3];
        command.p3 = data.size() == 4 ? -1 : data[4];
        if (data.size() > 5) {
            size_t lc = data[4];
            if (lc == 0 || (data.size() != 5 + lc && data.size() != 6 + lc))
                return error(se::ISecureElement::UNSUPPORTED_OPERATION);
            command.data = hex(Bytes(data.begin() + 5, data.end()));
        }
        Reply r;
        if (!request([this, channel, &command](int s) {
            return channel ? backend->iccTransmitApduLogicalChannel(s, command)
                           : backend->iccTransmitApduBasicChannel(s, command);
        }, &r)) return error(se::ISecureElement::CHANNEL_NOT_AVAILABLE);
        if (!success(r) || !unhex(r.text, out)) return error(se::ISecureElement::CHANNEL_NOT_AVAILABLE);
        out->push_back(r.sw1);
        out->push_back(r.sw2);
        return Status::ok();
    }
public:
    SecureElement() : backendName(std::string(sim::IRadioSim::descriptor) + "/se1") {
        std::thread([this] {
            for (;;) {
                std::unique_lock lock(notifications);
                notificationReady.wait(lock, [this] { return !events.empty(); });
                auto event = events.front();
                events.pop_front();
                lock.unlock();
                event.cb->onStateChange(event.state, event.reason);
            }
        }).detach();
    }
    void monitor() {
        for (;;) {
            {
                std::lock_guard lock(operation);
                if (client && AIBinder_ping(client->asBinder().get()) != STATUS_OK) {
                    closeAll();
                    client.reset();
                    owner = static_cast<uid_t>(-1);
                }
                if ((activeGeneration != generation || invalidated.exchange(false)) && backend) discard();
                if (connect()) {
                    Reply r;
                    bool present = card(&r) && r.value == sim::CardStatus::STATE_PRESENT;
                    if (!present && connected) { channels.clear(); notify(false, "SIM unavailable"); }
                    bool changed = dirty.exchange(false);
                    if (present && (!connected || changed)) notify(true, "SIM ready");
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    Status init(const std::shared_ptr<se::ISecureElementCallback>& cb) override {
        if (!cb) return Status::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        std::lock_guard lock(operation);
        if (client && owner != AIBinder_getCallingUid() && AIBinder_isAlive(client->asBinder().get()))
            return Status::fromExceptionCode(EX_SECURITY);
        closeAll();
        client = cb;
        owner = AIBinder_getCallingUid();
        Reply r;
        notify(card(&r) && r.value == sim::CardStatus::STATE_PRESENT, "Initialization");
        return Status::ok();
    }
    Status getAtr(Bytes* out) override {
        std::lock_guard lock(operation);
        out->clear();
        if (!owned()) return Status::fromExceptionCode(EX_SECURITY);
        Reply r;
        if (!card(&r) || !unhex(r.text, out)) return error();
        return Status::ok();
    }
    Status isCardPresent(bool* out) override {
        std::lock_guard lock(operation);
        Reply r;
        *out = card(&r) && r.value == sim::CardStatus::STATE_PRESENT;
        return Status::ok();
    }
    Status openLogicalChannel(const Bytes& aid, int8_t p2, se::LogicalChannelResponse* out) override {
        std::lock_guard lock(operation);
        if (!owned()) return Status::fromExceptionCode(EX_SECURITY);
        if (!aid.empty() && (aid.size() < 5 || aid.size() > 16)) return error(se::ISecureElement::NO_SUCH_ELEMENT_ERROR);
        Reply r;
        if (!request([this, &aid, p2](int s) { return backend->iccOpenLogicalChannel(s, hex(aid), static_cast<uint8_t>(p2)); }, &r)) return error();
        auto status = mapped(r);
        if (!status.isOk()) return status;
        if (r.value < 1 || r.value > 19 || channels.count(r.value)) { discard(); return error(); }
        channels[r.value] = owner;
        out->channelNumber = r.value;
        out->selectResponse = r.bytes;
        return Status::ok();
    }
    Status openBasicChannel(const Bytes& aid, int8_t p2, Bytes* out) override {
        std::lock_guard lock(operation);
        if (!owned()) return Status::fromExceptionCode(EX_SECURITY);
        if (!connect()) return error();
        if (channels.count(0)) return error(se::ISecureElement::CHANNEL_NOT_AVAILABLE);
        if (!aid.empty() && (aid.size() < 5 || aid.size() > 16)) return error(se::ISecureElement::NO_SUCH_ELEMENT_ERROR);
        if (!connect()) return error();
        channels[0] = owner;
        if (aid.empty()) { out->clear(); return Status::ok(); }
        Bytes select{0, 0xa4, 4, static_cast<uint8_t>(p2), static_cast<uint8_t>(aid.size())};
        select.insert(select.end(), aid.begin(), aid.end());
        auto status = apdu(select, out);
        for (int count = 0; status.isOk() && out->size() >= 2 && count < 32; ++count) {
            uint8_t sw1 = (*out)[out->size() - 2], sw2 = out->back();
            if (sw1 != 0x61 && sw1 != 0x9f) break;
            out->resize(out->size() - 2);
            Bytes next;
            status = apdu(Bytes{0, 0xc0, 0, 0, sw2}, &next);
            out->insert(out->end(), next.begin(), next.end());
        }
        if (status.isOk() && out->size() >= 2) {
            int sw = ((*out)[out->size() - 2] << 8) | out->back();
            if (sw == 0x6a82) status = error(se::ISecureElement::NO_SUCH_ELEMENT_ERROR);
            else if (sw == 0x6a86) status = error(se::ISecureElement::UNSUPPORTED_OPERATION);
            else if (sw != 0x9000 && (sw >> 8) != 0x62 && (sw >> 8) != 0x63)
                status = error();
        }
        if (!status.isOk()) channels.erase(0);
        return status;
    }
    Status transmit(const Bytes& data, Bytes* out) override {
        std::lock_guard lock(operation);
        out->clear();
        if (!owned()) return Status::fromExceptionCode(EX_SECURITY);
        return apdu(data, out);
    }
    Status closeChannel(int8_t number) override {
        std::lock_guard lock(operation);
        int channel = static_cast<uint8_t>(number);
        if (!owned()) return Status::fromExceptionCode(EX_SECURITY);
        if (!channels.count(channel)) return error(se::ISecureElement::FAILED);
        if ((generation != activeGeneration || invalidated)) { channels.clear(); return error(); }
        if (channel) {
            Reply r;
            if (!request([this, channel](int s) { return backend->iccCloseLogicalChannel(s, channel); }, &r)) return error();
            auto status = mapped(r);
            if (!status.isOk()) return status;
        }
        channels.erase(channel);
        return Status::ok();
    }
    Status reset() override {
        std::lock_guard lock(operation);
        if (!owned()) return Status::fromExceptionCode(EX_SECURITY);
        channels.clear();
        notify(false, "Reset");
        Reply down, up, state;
        bool off = request([this](int s) { return backend->setSimCardPower(s, sim::CardPowerState::POWER_DOWN); }, &down, true) && success(down);
        bool on = request([this](int s) { return backend->setSimCardPower(s, sim::CardPowerState::POWER_UP); }, &up, true) && success(up);
        dirty = true;
        if (!off || !on) return error(se::ISecureElement::FAILED);
        if (card(&state) && state.value == sim::CardStatus::STATE_PRESENT) notify(true, "Reset complete");
        return Status::ok();
    }
};

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    std::string name = std::string(se::ISecureElement::descriptor) + "/SIM1";
    if (!AServiceManager_isDeclared(name.c_str())) return 1;
    auto service = ndk::SharedRefBase::make<SecureElement>();
    if (AServiceManager_addService(service->asBinder().get(), name.c_str()) != STATUS_OK) return 1;
    std::thread([service] { service->monitor(); }).detach();
    ABinderProcess_joinThreadPool();
    return 1;
}
