/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SoundTriggerMindone.h"

#include <aidl/android/media/soundtrigger/RecognitionStatus.h>
#include <aidl/android/media/soundtrigger/Status.h>
#include <android-base/logging.h>
#include <android/binder_ibinder.h>
#include <hardware/sound_trigger.h>
#include <media/AidlConversionCppNdk.h>
#include <media/AidlConversionNdk.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace aidl::android::hardware::soundtrigger3::mindone {
namespace {

constexpr size_t kMaxData = 16 * 1024 * 1024;
using Status = ndk::ScopedAStatus;

Status status(int rc) {
    if (rc == 0) return Status::ok();
    if (rc == -ENOSYS || rc == -EOPNOTSUPP) {
        return Status::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    if (rc == -EINVAL) return Status::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    if (rc == -EBUSY || rc == -ENOMEM || rc == -ENOSPC || rc == -EAGAIN) {
        return Status::fromServiceSpecificError(static_cast<int32_t>(st::Status::RESOURCE_CONTENTION));
    }
    LOG(ERROR) << "Legacy soundtrigger error " << rc;
    return Status::fromServiceSpecificError(static_cast<int32_t>(st::Status::INTERNAL_ERROR));
}

bool parseUuid(const std::string& text, sound_trigger_uuid_t* uuid) {
    if (text.size() != 36) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? text[i] != '-' : !std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    unsigned int low, mid, high, seq, node[6];
    if (std::sscanf(text.c_str(), "%8x-%4x-%4x-%4x-%2x%2x%2x%2x%2x%2x", &low, &mid,
                    &high, &seq, &node[0], &node[1], &node[2], &node[3], &node[4], &node[5]) != 10) {
        return false;
    }
    uuid->timeLow = low;
    uuid->timeMid = mid;
    uuid->timeHiAndVersion = high;
    uuid->clockSeq = seq;
    for (size_t i = 0; i < 6; ++i) uuid->node[i] = node[i];
    return true;
}

std::string formatUuid(const sound_trigger_uuid_t& uuid) {
    char text[37];
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
                  uuid.timeLow, uuid.timeMid, uuid.timeHiAndVersion, uuid.clockSeq,
                  uuid.node[0], uuid.node[1], uuid.node[2], uuid.node[3], uuid.node[4], uuid.node[5]);
    return text;
}

bool extraToLegacy(const st::PhraseRecognitionExtra& src,
                   sound_trigger_phrase_recognition_extra* dst) {
    if (src.id < 0 || src.confidenceLevel < 0 || src.confidenceLevel > 100 ||
        src.levels.size() > SOUND_TRIGGER_MAX_USERS || (src.recognitionModes & ~15) != 0) return false;
    dst->id = src.id;
    dst->recognition_modes = src.recognitionModes;
    dst->confidence_level = src.confidenceLevel;
    dst->num_levels = src.levels.size();
    for (size_t i = 0; i < src.levels.size(); ++i) {
        if (src.levels[i].userId < 0 || src.levels[i].levelPercent < 0 ||
            src.levels[i].levelPercent > 100) return false;
        dst->levels[i].user_id = src.levels[i].userId;
        dst->levels[i].level = src.levels[i].levelPercent;
    }
    return true;
}

}

struct SoundTriggerMindone::Impl : std::enable_shared_from_this<Impl> {
    struct Client;
    struct RecognitionCookie {
        Client* client;
        uint64_t epoch;
    };
    struct Client {
        std::weak_ptr<Impl> owner;
        int32_t id = 0;
        sound_model_handle_t hal = 0;
        st::SoundModelType type = st::SoundModelType::INVALID;
        std::shared_ptr<ISoundTriggerHwCallback> callback;
        std::vector<st::Phrase> phrases;
        bool loaded = false;
        bool active = false;
        size_t drainers = 0;
        size_t deliveries = 0;
        uint64_t epoch = 0;
        ndk::SpAIBinder binder;
        std::vector<std::unique_ptr<RecognitionCookie>> recognitions;
    };
    struct DeathCookie {
        std::weak_ptr<Impl> owner;
        int32_t id;
        uint64_t attachment;
    };
    sound_trigger_hw_device* device = nullptr;
    std::mutex api;
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable drained;
    std::deque<std::function<void()>> jobs;
    std::unordered_map<int32_t, std::unique_ptr<Client>> clients;
    std::shared_ptr<ISoundTriggerHwGlobalCallback> global;
    ndk::SpAIBinder globalBinder;
    ndk::ScopedAIBinder_DeathRecipient death{AIBinder_DeathRecipient_new(died)};
    std::thread dispatcher;
    int64_t nextId = 1;
    uint64_t attachment = 0;
    bool quitting = false;
    bool captureBlocked = false;
    bool contention = false;
    st::Properties properties;

    Impl() {
        AIBinder_DeathRecipient_setOnUnlinked(death.get(), [](void* cookie) {
            delete static_cast<DeathCookie*>(cookie);
        });
    }

    static void died(void* cookie) {
        auto* link = static_cast<DeathCookie*>(cookie);
        if (auto self = link->owner.lock()) {
            const auto id = link->id;
            const auto generation = link->attachment;
            self->enqueue([self, id, generation] { self->cleanup(id, generation); });
        }
    }

    bool link(const ndk::SpAIBinder& binder, int32_t id) {
        if (!AIBinder_isRemote(binder.get())) return true;
        auto* cookie = new DeathCookie{weak_from_this(), id, attachment};
        return AIBinder_linkToDeath(binder.get(), death.get(), cookie) == STATUS_OK;
    }

    void enqueue(std::function<void()> job) {
        std::lock_guard lock(mutex);
        if (!quitting) {
            jobs.push_back(std::move(job));
            ready.notify_one();
        }
    }

    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] { return quitting || !jobs.empty(); });
                if (quitting && jobs.empty()) return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
        }
    }

    Client* find(int32_t id) {
        auto it = clients.find(id);
        return it != clients.end() && it->second->loaded ? it->second.get() : nullptr;
    }

    void resources() {
        std::shared_ptr<ISoundTriggerHwGlobalCallback> callback;
        {
            std::lock_guard lock(mutex);
            if (!contention) return;
            contention = false;
            callback = global;
        }
        if (callback) enqueue([callback] { callback->onResourcesAvailable(); });
    }

    Status result(int rc) {
        if (rc == -EBUSY || rc == -ENOMEM || rc == -ENOSPC || rc == -EAGAIN) {
            std::lock_guard lock(mutex);
            contention = true;
        }
        return status(rc);
    }

    int stop(Client* client) {
        uint64_t oldEpoch;
        {
            std::lock_guard lock(mutex);
            if (!client->active) {
                ++client->epoch;
                return 0;
            }
            client->active = false;
            oldEpoch = client->epoch;
            ++client->epoch;
        }
        const int rc = device->stop_recognition(device, client->hal);
        if (rc != 0) {
            std::lock_guard lock(mutex);
            client->active = true;
            client->epoch = oldEpoch;
        }
        return rc;
    }

    int unload(Client* client) {
        int rc = stop(client);
        if (rc != 0) return rc;
        rc = device->unload_sound_model(device, client->hal);
        if (rc == 0) {
            std::lock_guard lock(mutex);
            client->loaded = false;
            ++client->epoch;
            client->callback.reset();
            client->phrases.clear();
            client->binder = {};
        }
        return rc;
    }

    void cleanup(int32_t id, uint64_t generation) {
        std::lock_guard operation(api);
        if (generation != attachment) return;
        for (auto& [key, client] : clients) {
            if ((id == 0 || id == key) && client->loaded && unload(client.get()) != 0) {
                LOG(FATAL) << "Cannot clean dead soundtrigger client " << key;
            }
        }
        if (id == 0) {
            std::lock_guard lock(mutex);
            global.reset();
            globalBinder = {};
        }
        resources();
    }

    static void modelEvent(sound_trigger_model_event* event, void*) {
        if (event) LOG(INFO) << "Legacy sound model update " << event->model << ": " << event->status;
    }

    static void recognition(sound_trigger_recognition_event* src, void* cookie) {
        auto* recognitionCookie = static_cast<RecognitionCookie*>(cookie);
        auto* client = recognitionCookie->client;
        auto self = client->owner.lock();
        if (!self || !src) return;
        const bool phrase = client->type == st::SoundModelType::KEYPHRASE;
        const size_t minimum = phrase ? sizeof(sound_trigger_phrase_recognition_event) : sizeof(*src);
        if (src->type != static_cast<sound_trigger_sound_model_type_t>(client->type) ||
            src->data_size > kMaxData || (src->data_size != 0 &&
            (src->data_offset < minimum || src->data_offset > kMaxData - src->data_size)) ||
            src->status < RECOGNITION_STATUS_SUCCESS || src->status > RECOGNITION_STATUS_GET_STATE_RESPONSE) {
            LOG(ERROR) << "Malformed legacy soundtrigger event";
            return;
        }
        st::PhraseRecognitionEvent event;
        auto& out = event.common;
        out.status = static_cast<st::RecognitionStatus>(src->status);
        out.type = client->type;
        out.captureAvailable = src->capture_available;
        out.captureDelayMs = src->capture_delay_ms;
        out.capturePreambleMs = src->capture_preamble_ms;
        out.triggerInData = src->trigger_in_data;
        out.recognitionStillActive = src->status == RECOGNITION_STATUS_GET_STATE_RESPONSE;
        if (src->capture_available || src->trigger_in_data) {
            auto config = ::aidl::android::legacy2aidl_audio_config_t_AudioConfig(src->audio_config, true);
            if (!config.ok()) return;
            out.audioConfig = std::move(config.value());
        }
        if (src->data_size != 0) {
            const auto* data = reinterpret_cast<const uint8_t*>(src) + src->data_offset;
            out.data.assign(data, data + src->data_size);
        }
        if (phrase) {
            const auto* input = reinterpret_cast<const sound_trigger_phrase_recognition_event*>(src);
            if (input->num_phrases > SOUND_TRIGGER_MAX_PHRASES) return;
            for (size_t i = 0; i < input->num_phrases; ++i) {
                const auto& extra = input->phrase_extras[i];
                if (extra.num_levels > SOUND_TRIGGER_MAX_USERS) return;
                st::PhraseRecognitionExtra converted;
                converted.id = extra.id;
                converted.recognitionModes = extra.recognition_modes;
                converted.confidenceLevel = extra.confidence_level;
                for (size_t j = 0; j < extra.num_levels; ++j) {
                    st::ConfidenceLevel level;
                    level.userId = extra.levels[j].user_id;
                    level.levelPercent = extra.levels[j].level;
                    converted.levels.push_back(level);
                }
                event.phraseExtras.push_back(std::move(converted));
            }
        }
        uint64_t epoch;
        uint64_t generation;
        {
            std::lock_guard lock(self->mutex);
            if (!client->loaded || !client->active || client->hal != src->model ||
                recognitionCookie->epoch != client->epoch) return;
            epoch = client->epoch;
            generation = self->attachment;
            if (!out.recognitionStillActive) client->active = false;
        }
        self->enqueue([self, client, epoch, generation, phrase, event = std::move(event)] {
            std::shared_ptr<ISoundTriggerHwCallback> callback;
            {
                std::lock_guard lock(self->mutex);
                if (!client->loaded || client->epoch != epoch) return;
                callback = client->callback;
                if (callback) ++client->deliveries;
            }
            if (callback) {
                const auto rc = phrase ? callback->phraseRecognitionCallback(client->id, event)
                                       : callback->recognitionCallback(client->id, event.common);
                {
                    std::lock_guard lock(self->mutex);
                    --client->deliveries;
                    self->drained.notify_all();
                }
                if (!rc.isOk()) self->enqueue([self, id = client->id, generation] {
                    self->cleanup(id, generation);
                });
            }
            self->resources();
        });
    }

    Status load(const st::SoundModel& model, const std::vector<st::Phrase>* phrases,
                const std::shared_ptr<ISoundTriggerHwCallback>& callback, int32_t* handle) {
        if (!callback || model.dataSize < 0 || static_cast<size_t>(model.dataSize) > kMaxData ||
            (model.dataSize != 0 && model.data.get() < 0) ||
            model.type != (phrases ? st::SoundModelType::KEYPHRASE : st::SoundModelType::GENERIC)) {
            return status(-EINVAL);
        }
        if (phrases && (phrases->empty() || phrases->size() > SOUND_TRIGGER_MAX_PHRASES)) return status(-EINVAL);
        if (!device) return status(-ENODEV);
        const size_t offset = phrases ? sizeof(sound_trigger_phrase_sound_model) : sizeof(sound_trigger_sound_model);
        std::unique_ptr<void, decltype(&std::free)> buffer(std::calloc(1, offset + model.dataSize), std::free);
        if (!buffer) return result(-ENOMEM);
        auto* legacy = static_cast<sound_trigger_sound_model*>(buffer.get());
        legacy->type = static_cast<sound_trigger_sound_model_type_t>(model.type);
        if (!parseUuid(model.uuid, &legacy->uuid) || !parseUuid(model.vendorUuid, &legacy->vendor_uuid)) return status(-EINVAL);
        legacy->data_offset = offset;
        legacy->data_size = model.dataSize;
        size_t read = 0;
        while (read < static_cast<size_t>(model.dataSize)) {
            const auto n = pread(model.data.get(), static_cast<char*>(buffer.get()) + offset + read,
                                 model.dataSize - read, read);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return status(-EINVAL);
            read += n;
        }
        if (phrases) {
            auto* phraseModel = static_cast<sound_trigger_phrase_sound_model*>(buffer.get());
            phraseModel->num_phrases = phrases->size();
            for (size_t i = 0; i < phrases->size(); ++i) {
                const auto& src = (*phrases)[i];
                auto& dst = phraseModel->phrases[i];
                if (src.id < 0 || src.users.size() > SOUND_TRIGGER_MAX_USERS ||
                    src.locale.size() >= sizeof(dst.locale) || src.text.size() >= sizeof(dst.text) ||
                    src.locale.find('\0') != std::string::npos || src.text.find('\0') != std::string::npos ||
                    (src.recognitionModes & ~15) != 0) return status(-EINVAL);
                dst.id = src.id;
                dst.recognition_mode = src.recognitionModes;
                dst.num_users = src.users.size();
                for (size_t j = 0; j < src.users.size(); ++j) {
                    if (src.users[j] < 0) return status(-EINVAL);
                    dst.users[j] = src.users[j];
                }
                std::memcpy(dst.locale, src.locale.c_str(), src.locale.size() + 1);
                std::memcpy(dst.text, src.text.c_str(), src.text.size() + 1);
            }
        }
        std::lock_guard operation(api);
        {
            std::lock_guard lock(mutex);
            if (!global) return Status::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        size_t loaded = 0;
        for (const auto& entry : clients) loaded += entry.second->loaded;
        if (properties.maxSoundModels <= 0 || loaded >= static_cast<size_t>(properties.maxSoundModels) ||
            nextId > INT32_MAX) return result(-ENOSPC);
        auto client = std::make_unique<Client>();
        client->owner = weak_from_this();
        client->id = nextId++;
        client->type = model.type;
        client->callback = callback;
        if (phrases) client->phrases = *phrases;
        client->binder = callback->asBinder();
        auto* raw = client.get();
        clients.emplace(client->id, std::move(client));
        if (!link(raw->binder, raw->id)) {
            raw->callback.reset();
            raw->phrases.clear();
            raw->binder = {};
            return status(-EPIPE);
        }
        const int rc = device->load_sound_model(device, legacy, modelEvent, raw, &raw->hal);
        {
            std::lock_guard lock(mutex);
            raw->loaded = rc == 0;
            if (rc != 0) {
                raw->callback.reset();
                raw->phrases.clear();
                raw->binder = {};
            }
        }
        if (rc == 0) *handle = raw->id;
        return result(rc);
    }
};

SoundTriggerMindone::SoundTriggerMindone() : mImpl(std::make_shared<Impl>()) {}

SoundTriggerMindone::~SoundTriggerMindone() {
    auto& s = *mImpl;
    if (s.device) s.cleanup(0, s.attachment);
    {
        std::lock_guard lock(s.mutex);
        s.quitting = true;
        s.ready.notify_all();
    }
    if (s.dispatcher.joinable()) s.dispatcher.join();
    if (s.device) sound_trigger_hw_device_close(s.device);
}

bool SoundTriggerMindone::initialize() {
    auto& s = *mImpl;
    const hw_module_t* module = nullptr;
    int rc = hw_get_module(SOUND_TRIGGER_HARDWARE_MODULE_ID, &module);
    if (rc != 0) rc = hw_get_module_by_class(SOUND_TRIGGER_HARDWARE_MODULE_ID,
                                            SOUND_TRIGGER_HARDWARE_MODULE_ID_PRIMARY, &module);
    if (rc != 0 || sound_trigger_hw_device_open(module, &s.device) != 0 || !s.device) {
        s.device = nullptr;
        return false;
    }
    st::Properties properties;
    if (s.device->common.version != SOUND_TRIGGER_DEVICE_API_VERSION_1_3 ||
        !s.device->get_properties || !s.device->load_sound_model || !s.device->unload_sound_model ||
        !s.device->stop_recognition || !s.device->start_recognition ||
        !getProperties(&properties).isOk()) {
        sound_trigger_hw_device_close(s.device);
        s.device = nullptr;
        return false;
    }
    s.properties = std::move(properties);
    s.dispatcher = std::thread([impl = mImpl] { impl->run(); });
    return true;
}

Status SoundTriggerMindone::getProperties(st::Properties* out) {
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    *out = {};
    if (!s.device) return status(-ENODEV);
    sound_trigger_properties base = {};
    const sound_trigger_properties_extended_1_3* ext = nullptr;
    if (s.device->get_properties_extended) {
        const auto* header = s.device->get_properties_extended(s.device);
        if (header && header->version == SOUND_TRIGGER_DEVICE_API_VERSION_1_3 &&
            header->size >= sizeof(*ext)) ext = reinterpret_cast<const sound_trigger_properties_extended_1_3*>(header);
    }
    if (ext) base = ext->base;
    else {
        const int rc = s.device->get_properties(s.device, &base);
        if (rc != 0) return status(rc);
    }
    out->implementor.assign(base.implementor, strnlen(base.implementor, sizeof(base.implementor)));
    out->description.assign(base.description, strnlen(base.description, sizeof(base.description)));
    out->uuid = formatUuid(base.uuid);
    out->version = base.version;
    out->maxSoundModels = base.max_sound_models;
    out->maxKeyPhrases = base.max_key_phrases;
    out->maxUsers = base.max_users;
    out->recognitionModes = base.recognition_modes;
    out->captureTransition = base.capture_transition;
    out->maxBufferMs = base.max_buffer_ms;
    out->concurrentCapture = base.concurrent_capture;
    out->triggerInEvent = base.trigger_in_event;
    out->powerConsumptionMw = base.power_consumption_mw;
    if (ext) {
        out->supportedModelArch.assign(ext->supported_model_arch,
                strnlen(ext->supported_model_arch, sizeof(ext->supported_model_arch)));
        out->audioCapabilities = ext->audio_capabilities;
    }
    return Status::ok();
}

Status SoundTriggerMindone::registerGlobalCallback(const std::shared_ptr<ISoundTriggerHwGlobalCallback>& callback) {
    if (!callback) return status(-EINVAL);
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    if (!s.device) return status(-ENODEV);
    {
        std::lock_guard lock(s.mutex);
        if (s.global) return Status::fromExceptionCode(EX_ILLEGAL_STATE);
        ++s.attachment;
        s.global = callback;
        s.globalBinder = callback->asBinder();
    }
    if (!s.link(s.globalBinder, 0)) {
        std::lock_guard lock(s.mutex);
        s.global.reset();
        s.globalBinder = {};
        return status(-EPIPE);
    }
    return Status::ok();
}

Status SoundTriggerMindone::loadSoundModel(const st::SoundModel& model,
        const std::shared_ptr<ISoundTriggerHwCallback>& callback, int32_t* handle) {
    return mImpl->load(model, nullptr, callback, handle);
}

Status SoundTriggerMindone::loadPhraseSoundModel(const st::PhraseSoundModel& model,
        const std::shared_ptr<ISoundTriggerHwCallback>& callback, int32_t* handle) {
    return mImpl->load(model.common, &model.phrases, callback, handle);
}

Status SoundTriggerMindone::unloadSoundModel(int32_t handle) {
    auto& s = *mImpl;
    std::unique_lock operation(s.api);
    auto* client = s.find(handle);
    if (!client) return Status::ok();
    {
        std::lock_guard lock(s.mutex);
        ++client->drainers;
    }
    const int rc = s.unload(client);
    operation.unlock();
    {
        std::unique_lock lock(s.mutex);
        s.drained.wait(lock, [&] { return client->deliveries == 0; });
        --client->drainers;
    }
    if (rc == 0) s.resources();
    return status(rc);
}

Status SoundTriggerMindone::startRecognition(int32_t handle, int32_t deviceHandle, int32_t ioHandle,
                                            const st::RecognitionConfig& config) {
    if (config.phraseRecognitionExtras.size() > SOUND_TRIGGER_MAX_PHRASES || config.data.size() > kMaxData ||
        (config.audioCapabilities & ~3) != 0 || !audio_is_input_device(static_cast<audio_devices_t>(deviceHandle)) ||
        (config.captureRequested && ioHandle <= 0)) return status(-EINVAL);
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    auto* client = s.find(handle);
    if (!client) return status(-EINVAL);
    {
        std::lock_guard lock(s.mutex);
        if (client->drainers != 0) {
            s.contention = true;
            return status(-EBUSY);
        }
        if (client->active) return Status::fromExceptionCode(EX_ILLEGAL_STATE);
        if (s.captureBlocked && !s.properties.concurrentCapture) {
            s.contention = true;
            return status(-EBUSY);
        }
    }
    if (config.phraseRecognitionExtras.size() > client->phrases.size()) return status(-EINVAL);
    std::vector<int32_t> ids;
    for (const auto& extra : config.phraseRecognitionExtras) {
        const auto phrase = std::find_if(client->phrases.begin(), client->phrases.end(),
                [&](const st::Phrase& candidate) { return candidate.id == extra.id; });
        if (phrase == client->phrases.end() ||
            (extra.recognitionModes & ~phrase->recognitionModes) != 0 ||
            std::find(ids.begin(), ids.end(), extra.id) != ids.end()) return status(-EINVAL);
        ids.push_back(extra.id);
        for (const auto& level : extra.levels) {
            if (std::find(phrase->users.begin(), phrase->users.end(), level.userId) == phrase->users.end()) return status(-EINVAL);
        }
    }
    const bool extended = s.device->start_recognition_extended != nullptr;
    if (!extended && config.audioCapabilities != 0) return status(-ENOSYS);
    const size_t offset = extended ? sizeof(sound_trigger_recognition_config_extended_1_3)
                                   : sizeof(sound_trigger_recognition_config);
    std::unique_ptr<void, decltype(&std::free)> buffer(std::calloc(1, offset + config.data.size()), std::free);
    if (!buffer) return s.result(-ENOMEM);
    auto* ext = static_cast<sound_trigger_recognition_config_extended_1_3*>(buffer.get());
    auto* base = extended ? &ext->base : static_cast<sound_trigger_recognition_config*>(buffer.get());
    if (extended) {
        ext->header.version = SOUND_TRIGGER_DEVICE_API_VERSION_1_3;
        ext->header.size = offset + config.data.size();
        ext->audio_capabilities = config.audioCapabilities;
    }
    base->capture_handle = ioHandle;
    base->capture_device = static_cast<audio_devices_t>(deviceHandle);
    base->capture_requested = config.captureRequested;
    base->num_phrases = config.phraseRecognitionExtras.size();
    for (size_t i = 0; i < config.phraseRecognitionExtras.size(); ++i) {
        if (!extraToLegacy(config.phraseRecognitionExtras[i], &base->phrases[i])) return status(-EINVAL);
    }
    base->data_size = config.data.size();
    base->data_offset = offset;
    if (!config.data.empty()) std::memcpy(static_cast<char*>(buffer.get()) + offset, config.data.data(), config.data.size());
    Impl::RecognitionCookie* recognitionCookie;
    {
        std::lock_guard lock(s.mutex);
        ++client->epoch;
        client->active = true;
        auto cookie = std::make_unique<Impl::RecognitionCookie>();
        cookie->client = client;
        cookie->epoch = client->epoch;
        recognitionCookie = cookie.get();
        client->recognitions.push_back(std::move(cookie));
    }
    const int rc = extended ? s.device->start_recognition_extended(s.device, client->hal, &ext->header, Impl::recognition, recognitionCookie)
                            : s.device->start_recognition(s.device, client->hal, base, Impl::recognition, recognitionCookie);
    if (rc != 0) {
        std::lock_guard lock(s.mutex);
        client->active = false;
        ++client->epoch;
    }
    return s.result(rc);
}

Status SoundTriggerMindone::stopRecognition(int32_t handle) {
    auto& s = *mImpl;
    std::unique_lock operation(s.api);
    auto* client = s.find(handle);
    if (!client) return Status::ok();
    {
        std::lock_guard lock(s.mutex);
        ++client->drainers;
    }
    const int rc = s.stop(client);
    operation.unlock();
    {
        std::unique_lock lock(s.mutex);
        s.drained.wait(lock, [&] { return client->deliveries == 0; });
        --client->drainers;
    }
    if (rc == 0) s.resources();
    return status(rc);
}

Status SoundTriggerMindone::forceRecognitionEvent(int32_t handle) {
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    auto* client = s.find(handle);
    if (!client) return status(-EINVAL);
    {
        std::lock_guard lock(s.mutex);
        if (!client->active) return Status::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    return status(s.device->get_model_state ? s.device->get_model_state(s.device, client->hal) : -ENOSYS);
}

Status SoundTriggerMindone::queryParameter(int32_t handle, st::ModelParameter parameter,
                                           std::optional<st::ModelParameterRange>* out) {
    out->reset();
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    auto* client = s.find(handle);
    if (!client) return status(-EINVAL);
    if (parameter != st::ModelParameter::THRESHOLD_FACTOR || !s.device->query_parameter) return Status::ok();
    sound_trigger_model_parameter_range_t range = {};
    const int rc = s.device->query_parameter(s.device, client->hal, MODEL_PARAMETER_THRESHOLD_FACTOR, &range);
    if (rc == -ENOSYS || rc == -EOPNOTSUPP || rc == -EINVAL) return Status::ok();
    if (rc == 0 && range.is_supported) {
        if (range.start > range.end) return status(-EIO);
        st::ModelParameterRange converted;
        converted.minInclusive = range.start;
        converted.maxInclusive = range.end;
        *out = converted;
    }
    return status(rc);
}

Status SoundTriggerMindone::getParameter(int32_t handle, st::ModelParameter parameter, int32_t* out) {
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    auto* client = s.find(handle);
    if (!client) return status(-EINVAL);
    if (parameter != st::ModelParameter::THRESHOLD_FACTOR || !s.device->get_parameter) return status(-ENOSYS);
    return status(s.device->get_parameter(s.device, client->hal, MODEL_PARAMETER_THRESHOLD_FACTOR, out));
}

Status SoundTriggerMindone::setParameter(int32_t handle, st::ModelParameter parameter, int32_t value) {
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    auto* client = s.find(handle);
    if (!client) return status(-EINVAL);
    if (parameter != st::ModelParameter::THRESHOLD_FACTOR || !s.device->set_parameter) return status(-ENOSYS);
    return status(s.device->set_parameter(s.device, client->hal, MODEL_PARAMETER_THRESHOLD_FACTOR, value));
}

void SoundTriggerMindone::setCaptureBlocked(bool blocked) {
    auto& s = *mImpl;
    std::lock_guard operation(s.api);
    {
        std::lock_guard lock(s.mutex);
        s.captureBlocked = blocked;
    }
    if (blocked && !s.properties.concurrentCapture) {
        for (auto& [id, client] : s.clients) {
            bool active;
            {
                std::lock_guard lock(s.mutex);
                active = client->loaded && client->active;
            }
            if (!active) continue;
            if (s.stop(client.get()) != 0) LOG(FATAL) << "Cannot preempt soundtrigger capture";
            auto callback = client->callback;
            const bool phrase = client->type == st::SoundModelType::KEYPHRASE;
            uint64_t epoch;
            {
                std::lock_guard lock(s.mutex);
                epoch = client->epoch;
            }
            s.enqueue([impl = mImpl, raw = client.get(), callback, id, phrase, epoch] {
                {
                    std::lock_guard lock(impl->mutex);
                    if (!raw->loaded || raw->epoch != epoch) return;
                    ++raw->deliveries;
                }
                st::PhraseRecognitionEvent event;
                event.common.status = st::RecognitionStatus::ABORTED;
                event.common.type = phrase ? st::SoundModelType::KEYPHRASE : st::SoundModelType::GENERIC;
                if (phrase) callback->phraseRecognitionCallback(id, event);
                else callback->recognitionCallback(id, event.common);
                {
                    std::lock_guard lock(impl->mutex);
                    --raw->deliveries;
                    impl->drained.notify_all();
                }
            });
            std::lock_guard lock(s.mutex);
            s.contention = true;
        }
    }
    if (!blocked) s.resources();
}

}
