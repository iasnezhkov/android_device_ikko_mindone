// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#include "RadioCallbacks.h"
#include <aidl/android/hardware/secure_element/BnSecureElement.h>
#include <aidl/android/hardware/secure_element/ISecureElementCallback.h>
#include <aidl/android/hardware/radio/sim/IRadioSim.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_ibinder.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
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
    enum class CardQuery { OK, RADIO_ERROR, INTERRUPTED, DISCONNECTED };
    struct Reply {
        radio::RadioResponseInfo info;
        int value = 0;
        Bytes bytes;
        std::string text;
        int sw1 = 0, sw2 = 0;
    };
    struct BackendDeathCookie {
        std::weak_ptr<SecureElement> self;
        uint64_t epoch;
    };
    struct ClientDeathCookie {
        std::weak_ptr<SecureElement> self;
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
    bool readyReply = false, connected = false, stateKnown = false;
    Reply result;
    std::atomic<bool> dirty{true}, invalidated{false}, backendDead{false}, stopping{false};
    std::mutex monitorMutex;
    std::condition_variable monitorCv;
    bool monitorPending = false;
    bool resetBackoff = false;
    std::mutex notifications;
    std::condition_variable notificationReady;
    bool stopNotifier = false;
    struct Event { std::shared_ptr<se::ISecureElementCallback> cb; bool state; std::string reason; };
    std::deque<Event> events;
    std::thread notifierThread;
    std::atomic<uint64_t> generation{0};
    uint64_t activeGeneration = 0;
    ndk::ScopedAIBinder_DeathRecipient backendDeath;
    ndk::ScopedAIBinder_DeathRecipient clientDeath;
    void* backendDeathCookie = nullptr;
    void* clientDeathCookie = nullptr;

    void wakeMonitor(bool reset) {
        std::lock_guard lock(monitorMutex);
        monitorPending = true;
        if (reset) resetBackoff = true;
        monitorCv.notify_all();
    }
    void onBackendDied(uint64_t epoch) {
        if (epoch == generation.load()) {
            backendDead.store(true);
            dirty.store(true);
            {
                std::lock_guard lock(responses);
                completed.notify_all();
            }
            wakeMonitor(true);
        }
    }
    void unlinkBackend() {
        if (backend && backendDeathCookie) {
            AIBinder_unlinkToDeath(backend->asBinder().get(), backendDeath.get(), backendDeathCookie);
            backendDeathCookie = nullptr;
        }
    }
    void unlinkClient() {
        if (client && clientDeathCookie) {
            AIBinder_unlinkToDeath(client->asBinder().get(), clientDeath.get(), clientDeathCookie);
            clientDeathCookie = nullptr;
        }
    }
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
    void discard(bool wake = true) {
        bool hadBackend = static_cast<bool>(backend);
        unlinkBackend();
        backend.reset();
        channels.clear();
        stateKnown = false;
        dirty.store(true);
        ++generation;
        activeGeneration = generation.load();
        if (connected) notify(false, "Radio connection lost");
        if (wake && hadBackend) wakeMonitor(true);
    }
    bool connect(bool wakeOnDiscard = true) {
        if (backendDead.exchange(false) && backend) discard(wakeOnDiscard);
        if (backend && AIBinder_isAlive(backend->asBinder().get())) {
            if (invalidated.exchange(false)) {
                channels.clear();
                if (connected) notify(false, "SIM state changed");
            }
            return true;
        }
        if (backend) discard(wakeOnDiscard);
        ndk::SpAIBinder binder(AServiceManager_checkService(backendName.c_str()));
        if (!binder.get()) return false;
        auto candidate = sim::IRadioSim::fromBinder(binder);
        if (!candidate) return false;
        auto r = ndk::SharedRefBase::make<RadioSimResponse>();
        auto i = ndk::SharedRefBase::make<RadioSimIndication>();
        uint64_t epoch = ++generation;
        std::weak_ptr<SecureElement> weak(ref<SecureElement>());
        r->reply = [weak, epoch](const radio::RadioResponseInfo& info, int value,
                                 const Bytes& bytes, const std::string& text, int sw1, int sw2) {
            if (info.type == radio::RadioResponseType::SOLICITED_ACK) return;
            auto self = weak.lock();
            if (!self) return;
            std::lock_guard lock(self->responses);
            if (epoch != self->generation.load() || info.serial != self->pending) return;
            self->result = {info, value, bytes, text, sw1, sw2};
            self->readyReply = true;
            self->completed.notify_all();
        };
        i->acknowledge = [candidate] { candidate->responseAcknowledgement(); };
        i->changed = [weak, epoch] {
            auto self = weak.lock();
            if (!self) return;
            if (epoch == self->generation.load()) {
                self->dirty.store(true);
                self->invalidated.store(true);
                self->wakeMonitor(true);
            }
        };
        if (!candidate->setResponseFunctions(r, i).isOk()) return false;
        backend = candidate;
        response = r;
        indication = i;
        activeGeneration = epoch;
        stateKnown = false;
        dirty.store(true);
        invalidated.store(false);
        backendDead.store(false);
        if (AIBinder_isRemote(binder.get())) {
            auto* cookie = new BackendDeathCookie{weak, epoch};
            if (AIBinder_linkToDeath(binder.get(), backendDeath.get(), cookie) == STATUS_OK) {
                backendDeathCookie = cookie;
            }
        }
        return true;
    }
    template<class F> bool request(F fn, Reply* reply, bool allowStateChange = false, bool wakeOnDiscard = true) {
        if (!connect(wakeOnDiscard)) return false;
        uint64_t epoch = generation.load();
        {
            std::lock_guard lock(responses);
            if (serial == INT32_MAX) { discard(wakeOnDiscard); return false; }
            pending = ++serial;
            readyReply = false;
        }
        if (!fn(pending).isOk()) { discard(wakeOnDiscard); return false; }
        std::unique_lock lock(responses);
        bool done = completed.wait_for(lock, std::chrono::seconds(5), [this] {
            return readyReply || stopping.load() || backendDead.load();
        });
        if (stopping.load()) return false;
        if (!done || backendDead.load()) { lock.unlock(); discard(wakeOnDiscard); return false; }
        *reply = result;
        lock.unlock();
        if (epoch != generation.load() || (invalidated.load() && !allowStateChange)) return false;
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
    CardQuery queryCard(Reply* r, bool wakeOnDiscard = true) {
        if (!request([this](int s) { return backend->getIccCardStatus(s); }, r, false, wakeOnDiscard)) {
            if (backend && invalidated.load()) return CardQuery::INTERRUPTED;
            return CardQuery::DISCONNECTED;
        }
        return success(*r) ? CardQuery::OK : CardQuery::RADIO_ERROR;
    }
    bool card(Reply* r) {
        return queryCard(r) == CardQuery::OK;
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
        if (it == channels.end() || it->second != owner || (generation.load() != activeGeneration || invalidated.load()))
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
    SecureElement()
        : backendName(std::string(sim::IRadioSim::descriptor) + "/se1"),
          backendDeath(AIBinder_DeathRecipient_new([](void* data) {
              auto* cookie = static_cast<BackendDeathCookie*>(data);
              if (auto self = cookie->self.lock()) self->onBackendDied(cookie->epoch);
          })),
          clientDeath(AIBinder_DeathRecipient_new([](void* data) {
              auto* cookie = static_cast<ClientDeathCookie*>(data);
              if (auto self = cookie->self.lock()) self->wakeMonitor(false);
          })) {
        AIBinder_DeathRecipient_setOnUnlinked(backendDeath.get(), [](void* data) {
            delete static_cast<BackendDeathCookie*>(data);
        });
        AIBinder_DeathRecipient_setOnUnlinked(clientDeath.get(), [](void* data) {
            delete static_cast<ClientDeathCookie*>(data);
        });
        notifierThread = std::thread([this] {
            for (;;) {
                std::unique_lock lock(notifications);
                notificationReady.wait(lock, [this] { return stopNotifier || !events.empty(); });
                if (stopNotifier) break;
                auto event = std::move(events.front());
                events.pop_front();
                lock.unlock();
                event.cb->onStateChange(event.state, event.reason);
            }
        });
    }
    ~SecureElement() override {
        shutdown();
        unlinkBackend();
        unlinkClient();
    }
    void shutdown() {
        stopping.store(true);
        {
            std::lock_guard lock(monitorMutex);
            monitorPending = true;
            monitorCv.notify_all();
        }
        {
            std::lock_guard lock(responses);
            completed.notify_all();
        }
        {
            std::lock_guard lock(notifications);
            stopNotifier = true;
            notificationReady.notify_all();
        }
        if (notifierThread.joinable() && notifierThread.get_id() != std::this_thread::get_id()) {
            notifierThread.join();
        }
    }
    void monitor() {
        static constexpr std::array<std::chrono::seconds, 5> kBackoff{
            std::chrono::seconds(1),
            std::chrono::seconds(2),
            std::chrono::seconds(4),
            std::chrono::seconds(8),
            std::chrono::seconds(30),
        };
        size_t backoffIndex = 0;
        for (;;) {
            bool retry = false;
            bool immediate = false;
            {
                std::lock_guard lock(operation);
                if (stopping.load()) break;
                if (client && (!AIBinder_isAlive(client->asBinder().get()) ||
                               AIBinder_ping(client->asBinder().get()) != STATUS_OK)) {
                    closeAll();
                    unlinkClient();
                    client.reset();
                    owner = static_cast<uid_t>(-1);
                }
                if ((activeGeneration != generation.load() || backendDead.exchange(false)) && backend) {
                    discard(false);
                }
                bool simChanged = invalidated.exchange(false);
                if (simChanged) channels.clear();
                bool needConnect = !backend || !AIBinder_isAlive(backend->asBinder().get());
                bool needQuery = needConnect || simChanged || dirty.load() || !stateKnown;
                if (needQuery) {
                    if (!connect(false)) {
                        retry = true;
                    } else {
                        Reply r;
                        CardQuery q = queryCard(&r, false);
                        if (stopping.load()) break;
                        if (q == CardQuery::OK) {
                            bool present = r.value == sim::CardStatus::STATE_PRESENT;
                            stateKnown = true;
                            bool changed = dirty.exchange(false);
                            if (!present && connected) {
                                channels.clear();
                                notify(false, "SIM unavailable");
                            } else if (present) {
                                if (simChanged && connected) notify(false, "SIM state changed");
                                if (!connected || changed) notify(true, "SIM ready");
                            }
                            backoffIndex = 0;
                        } else if (q == CardQuery::INTERRUPTED) {
                            immediate = true;
                        } else {
                            if (connected) {
                                channels.clear();
                                notify(false, "SIM unavailable");
                            }
                            stateKnown = false;
                            retry = true;
                        }
                    }
                } else {
                    backoffIndex = 0;
                }
            }
            if (immediate) continue;
            std::unique_lock waitLock(monitorMutex);
            if (stopping.load()) break;
            if (monitorPending) {
                monitorPending = false;
                if (resetBackoff) {
                    resetBackoff = false;
                    backoffIndex = 0;
                }
                continue;
            }
            if (retry && backoffIndex < kBackoff.size()) {
                auto delay = kBackoff[backoffIndex++];
                monitorCv.wait_for(waitLock, delay, [this] {
                    return stopping.load() || monitorPending;
                });
                if (stopping.load()) break;
                if (monitorPending) {
                    monitorPending = false;
                    if (resetBackoff) {
                        resetBackoff = false;
                        backoffIndex = 0;
                    }
                }
            } else {
                monitorCv.wait(waitLock, [this] {
                    return stopping.load() || monitorPending;
                });
                if (stopping.load()) break;
                monitorPending = false;
                if (resetBackoff) {
                    resetBackoff = false;
                    backoffIndex = 0;
                }
            }
        }
    }
    Status init(const std::shared_ptr<se::ISecureElementCallback>& cb) override {
        if (!cb) return Status::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        std::lock_guard lock(operation);
        if (client && owner != AIBinder_getCallingUid() && AIBinder_isAlive(client->asBinder().get()))
            return Status::fromExceptionCode(EX_SECURITY);
        closeAll();
        unlinkClient();
        client = cb;
        owner = AIBinder_getCallingUid();
        auto binder = client->asBinder();
        if (binder.get() && AIBinder_isRemote(binder.get())) {
            auto* cookie = new ClientDeathCookie{ref<SecureElement>()};
            if (AIBinder_linkToDeath(binder.get(), clientDeath.get(), cookie) == STATUS_OK) {
                clientDeathCookie = cookie;
            }
        }
        Reply r;
        CardQuery q = queryCard(&r, false);
        if (q == CardQuery::OK) {
            stateKnown = true;
            dirty.store(false);
            notify(r.value == sim::CardStatus::STATE_PRESENT, "Initialization");
            wakeMonitor(true);
        } else {
            stateKnown = false;
            dirty.store(true);
            notify(false, "Initialization");
            wakeMonitor(true);
        }
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
        CardQuery q = queryCard(&r);
        *out = q == CardQuery::OK && r.value == sim::CardStatus::STATE_PRESENT;
        if (q != CardQuery::OK) wakeMonitor(true);
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
        if ((generation.load() != activeGeneration || invalidated.load())) { channels.clear(); return error(); }
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
        dirty.store(true);
        if (!off || !on) {
            stateKnown = false;
            wakeMonitor(true);
            return error(se::ISecureElement::FAILED);
        }
        if (queryCard(&state) == CardQuery::OK) {
            stateKnown = true;
            dirty.store(false);
            if (state.value == sim::CardStatus::STATE_PRESENT) notify(true, "Reset complete");
        } else {
            stateKnown = false;
            wakeMonitor(true);
        }
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
    std::thread monitorThread([service] { service->monitor(); });
    ABinderProcess_joinThreadPool();
    service->shutdown();
    monitorThread.join();
    return 1;
}
