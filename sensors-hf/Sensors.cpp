/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone.sensors"

#include "Sensors.h"

#if defined(MINDONE_HF_RUST_WIRE)
#include "HfRustCore.h"
#endif

#include <android/binder_status.h>
#include <log/log.h>
#include <utils/Errors.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>

namespace mindone {
namespace sensors {

using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::sensors::SensorType;
using ::mindone::hf::hf_manager_event;
using EventPayload = Event::EventPayload;

namespace {

constexpr int kReadyTimeoutMs = 40000;

constexpr uint32_t kEventQueueFlagBitsReadAndProcess = 1;
constexpr uint32_t kEventQueueFlagBitsEventsRead =
        static_cast<uint32_t>(ISensors::EVENT_QUEUE_FLAG_BITS_EVENTS_READ);

}

Sensors::Sensors() {
    if (!hf_.open()) {
        ALOGE("could not open /dev/hf_manager - sensors will report an empty list");
    }
}

Sensors::~Sensors() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopReaderLocked();
}

void Sensors::ensureEnumerated() {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureEnumeratedLocked();
}

void Sensors::ensureEnumeratedLocked() {
    if (enumerated_) return;
    if (!hf_.isOpen()) {
        enumerated_ = true;
        return;
    }

    hf_.waitUntilReady(kReadyTimeoutMs);

    sensorList_.clear();
    gainByHandle_.clear();
    wakeUpByHandle_.clear();

    for (int t = 1; t < ::mindone::hf::SENSOR_TYPE_SENSOR_MAX; ++t) {
        auto typeInfo = ::mindone::hf::lookupSensorTypeInfo(static_cast<uint8_t>(t));
        if (!typeInfo) continue;

        ::mindone::hf::sensor_info raw{};
        if (!hf_.querySensorInfo(static_cast<uint8_t>(t), &raw)) continue;

        SensorInfo info{};
        info.sensorHandle = t;
        info.name = std::string(raw.name, strnlen(raw.name, sizeof(raw.name)));
        info.vendor = std::string(raw.vendor, strnlen(raw.vendor, sizeof(raw.vendor)));
        info.version = 1;
        info.type = typeInfo->aidlType;
        info.typeAsString = typeInfo->typeAsString;
        info.maxRange = typeInfo->maxRange;
        info.resolution = typeInfo->resolution;
        info.power = typeInfo->power;
        info.minDelayUs = typeInfo->minDelayUs;
        info.fifoReservedEventCount = typeInfo->fifoReservedEventCount;
        info.fifoMaxEventCount = typeInfo->fifoMaxEventCount;
        info.requiredPermission = typeInfo->requiredPermission;
        info.maxDelayUs = typeInfo->maxDelayUs;
        info.flags = typeInfo->flags;

        sensorList_.push_back(info);
        gainByHandle_[t] = raw.gain ? raw.gain : 1;
        wakeUpByHandle_[t] = (typeInfo->flags & ::mindone::hf::kFlagWakeUp) != 0;
        if (!typeInfo->verified) {
            ALOGW("sensor %s (type=%d handle=%d): SensorInfo numeric fields are "
                  "PLACEHOLDERS, not verified against the real chip - see plan doc",
                  info.name.c_str(), t, t);
        }
    }

    ALOGI("hf_manager enumeration done: %zu AIDL-mappable sensors found", sensorList_.size());

    calibrationStore_.pushAll(hf_);

    enumerated_ = true;
}

void Sensors::startReaderLocked() {
    if (readerThread_.joinable()) return;
    stopReader_.store(false);
    readerThread_ = std::thread(&Sensors::readerLoop, this);
}

void Sensors::stopReaderLocked() {
    stopReader_.store(true);
    if (eventQueueFlag_) eventQueueFlag_->wake(kEventQueueFlagBitsEventsRead);
    if (readerThread_.joinable()) readerThread_.join();
    stopWakeLockThreadLocked();
    if (eventQueueFlag_)
        EventFlag::deleteEventFlag(&eventQueueFlag_);
}

void Sensors::readerLoop() {
    std::vector<hf_manager_event> kevents;
    while (!stopReader_.load()) {
        if (!hf_.waitAndReadEvents(5000, &kevents)) {
            ALOGE("hf_manager event stream broken - reader thread exiting");
            return;
        }
        if (kevents.empty()) continue;

        std::vector<Event> aidlEvents;
        aidlEvents.reserve(kevents.size());
        for (const auto& ke : kevents) {
            auto e = translateEvent(ke);
            if (e) aidlEvents.push_back(std::move(*e));
        }
        if (!aidlEvents.empty()) postEvents(aidlEvents);
    }
}

std::optional<Event> Sensors::translateEvent(const hf_manager_event& ke) {
    auto typeInfo = ::mindone::hf::lookupSensorTypeInfo(ke.sensor_type);
    if (!typeInfo) return std::nullopt;

    Event e{};
    e.timestamp = ke.timestamp;
    e.sensorHandle = ke.sensor_type;
    e.sensorType = typeInfo->aidlType;

    if (ke.action == ::mindone::hf::FLUSH_ACTION) {
        EventPayload::MetaData meta;
        meta.what = EventPayload::MetaData::MetaDataEventType::META_DATA_FLUSH_COMPLETE;
        e.sensorType = SensorType::META_DATA;
        e.payload.set<EventPayload::meta>(meta);
        return e;
    }
    if (ke.action != ::mindone::hf::DATA_ACTION) {
        calibrationStore_.onKernelEvent(ke);
        return std::nullopt;
    }

    auto gainIt = gainByHandle_.find(ke.sensor_type);
    const float gain = (gainIt != gainByHandle_.end() && gainIt->second != 0)
                                ? static_cast<float>(gainIt->second)
                                : 1.0f;
    auto scaled = [&](int idx) { return static_cast<float>(ke.word[idx]) / gain; };
    auto status = static_cast<::aidl::android::hardware::sensors::SensorStatus>(ke.accurancy);

    switch (typeInfo->aidlType) {
        case SensorType::ACCELEROMETER:
        case SensorType::MAGNETIC_FIELD:
        case SensorType::GYROSCOPE:
        case SensorType::GRAVITY:
        case SensorType::LINEAR_ACCELERATION:
        case SensorType::ORIENTATION: {
            EventPayload::Vec3 v3;
            v3.x = scaled(0);
            v3.y = scaled(1);
            v3.z = scaled(2);
            v3.status = status;
            e.payload.set<EventPayload::vec3>(v3);
            break;
        }
        case SensorType::MAGNETIC_FIELD_UNCALIBRATED:
        case SensorType::GYROSCOPE_UNCALIBRATED:
        case SensorType::ACCELEROMETER_UNCALIBRATED: {
            EventPayload::Uncal u;
            u.x = scaled(0);
            u.y = scaled(1);
            u.z = scaled(2);
            u.xBias = scaled(3);
            u.yBias = scaled(4);
            u.zBias = scaled(5);
            e.payload.set<EventPayload::uncal>(u);
            break;
        }
        case SensorType::GAME_ROTATION_VECTOR: {
            EventPayload::Vec4 v4;
            v4.x = scaled(0);
            v4.y = scaled(1);
            v4.z = scaled(2);
            v4.w = scaled(3);
            e.payload.set<EventPayload::vec4>(v4);
            break;
        }
        case SensorType::ROTATION_VECTOR:
        case SensorType::GEOMAGNETIC_ROTATION_VECTOR: {
            EventPayload::Data data{};
            for (int i = 0; i < 5; ++i) data.values[i] = scaled(i);
            e.payload.set<EventPayload::data>(data);
            break;
        }
        case SensorType::STEP_COUNTER:
            e.payload.set<EventPayload::stepCount>(static_cast<int64_t>(ke.word[0]));
            break;
        default:
            e.payload.set<EventPayload::scalar>(scaled(0));
            break;
    }
    return e;
}

void Sensors::postEvents(const std::vector<Event>& events) {
    constexpr int64_t kSpaceWaitNs = 5000LL * 1000 * 1000;
    size_t offset = 0;
    while (offset < events.size() && !stopReader_.load()) {
        bool retryDelay = false;
        {
            std::lock_guard<std::mutex> lock(writeLock_);
            if (!eventQueue_ || !eventQueueFlag_) return;
            const size_t count = std::min(events.size() - offset, eventQueue_->availableToWrite());
            if (count > 0) {
                int32_t wakeUp = 0;
                for (size_t i = offset; i < offset + count; ++i) {
                    auto it = wakeUpByHandle_.find(events[i].sensorHandle);
                    if (it != wakeUpByHandle_.end() && it->second) ++wakeUp;
                }
                std::lock_guard<std::mutex> wakeLock(wakeLockLock_);
                const int64_t previousDeadline = autoReleaseWakeLockTime_;
                if (wakeUp > 0) updateWakeLockLocked(wakeUp, 0);
                if ((wakeUp == 0 || hasWakeLock_) &&
                        eventQueue_->write(events.data() + offset, count)) {
                    eventQueueFlag_->wake(kEventQueueFlagBitsReadAndProcess);
                    offset += count;
                    continue;
                }
                if (wakeUp > 0) {
                    autoReleaseWakeLockTime_ = previousDeadline;
                    updateWakeLockLocked(0, wakeUp);
                }
                retryDelay = true;
            }
        }
        if (stopReader_.load()) break;
        if (retryDelay) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        uint32_t state = 0;
        const auto status = eventQueueFlag_->wait(kEventQueueFlagBitsEventsRead, &state,
                                                  kSpaceWaitNs, true);
        if (status != ::android::OK && status != ::android::TIMED_OUT) {
            ALOGE("hf_manager->AIDL: event FMQ wait failed: %d", status);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Sensors::startWakeLockThreadLocked() {
    if (wakeLockThread_.joinable()) return;
    readWakeLockQueueRun_.store(true);
    wakeLockThread_ = std::thread(&Sensors::readWakeLockFMQ, this);
}

void Sensors::stopWakeLockThreadLocked() {
    readWakeLockQueueRun_.store(false);
    if (wakeLockThread_.joinable()) wakeLockThread_.join();
    std::lock_guard<std::mutex> lock(wakeLockLock_);
    if (hasWakeLock_ && release_wake_lock(kWakeLockName) == 0) hasWakeLock_ = false;
    outstandingWakeUpEvents_ = 0;
}

void Sensors::readWakeLockFMQ() {
    constexpr int64_t kHeldTimeoutNs = 500LL * 1000 * 1000;
    constexpr int64_t kIdleTimeoutNs = 5000LL * 1000 * 1000;

    while (readWakeLockQueueRun_.load()) {
        int64_t timeoutNs;
        {
            std::lock_guard<std::mutex> lock(wakeLockLock_);
            timeoutNs = hasWakeLock_ ? kHeldTimeoutNs : kIdleTimeoutNs;
        }
        int32_t eventsHandled = 0;
        if (wakeLockQueue_) {
            wakeLockQueue_->readBlocking(
                    &eventsHandled, 1 , 0 ,
                    static_cast<uint32_t>(ISensors::WAKE_LOCK_QUEUE_FLAG_BITS_DATA_WRITTEN),
                    timeoutNs);
        } else {
            usleep(static_cast<useconds_t>(timeoutNs / 1000));
        }
        updateWakeLock(0 , eventsHandled);
    }
}

void Sensors::updateWakeLock(int32_t eventsWritten, int32_t eventsHandled) {
    std::lock_guard<std::mutex> lock(wakeLockLock_);
    updateWakeLockLocked(eventsWritten, eventsHandled);
}

void Sensors::updateWakeLockLocked(int32_t eventsWritten, int32_t eventsHandled) {
    int32_t newVal = outstandingWakeUpEvents_ + eventsWritten - eventsHandled;
    outstandingWakeUpEvents_ = newVal < 0 ? 0 : newVal;
    if (eventsWritten > 0) {
        autoReleaseWakeLockTime_ =
                ::android::uptimeMillis() +
                static_cast<int64_t>(ISensors::WAKE_LOCK_TIMEOUT_SECONDS) * 1000;
    }
    if (!hasWakeLock_ && outstandingWakeUpEvents_ > 0 &&
        acquire_wake_lock(PARTIAL_WAKE_LOCK, kWakeLockName) == 0) {
        hasWakeLock_ = true;
    } else if (hasWakeLock_) {
        if (::android::uptimeMillis() > autoReleaseWakeLockTime_) {
            ALOGD("no wake-lock FMQ acknowledgements for %d s, auto-releasing the wake lock",
                  static_cast<int>(ISensors::WAKE_LOCK_TIMEOUT_SECONDS));
            outstandingWakeUpEvents_ = 0;
        }
        if (outstandingWakeUpEvents_ == 0 && release_wake_lock(kWakeLockName) == 0) {
            hasWakeLock_ = false;
        }
    }
}


::ndk::ScopedAStatus Sensors::getSensorsList(std::vector<SensorInfo>* _aidl_return) {
    ensureEnumerated();
    std::lock_guard<std::mutex> lock(mutex_);
    *_aidl_return = sensorList_;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::initialize(
        const MQDescriptor<Event, SynchronizedReadWrite>& in_eventQueueDescriptor,
        const MQDescriptor<int32_t, SynchronizedReadWrite>& in_wakeLockDescriptor,
        const std::shared_ptr<ISensorsCallback>& in_sensorsCallback) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopReaderLocked();

    eventQueue_ = std::make_unique<AidlMessageQueue<Event, SynchronizedReadWrite>>(
            in_eventQueueDescriptor);
    wakeLockQueue_ = std::make_unique<AidlMessageQueue<int32_t, SynchronizedReadWrite>>(
            in_wakeLockDescriptor);
    if (!eventQueue_->isValid() || !wakeLockQueue_->isValid()) {
        ALOGE("initialize(): invalid FMQ descriptor");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    eventQueueFlag_ = nullptr;
    if (EventFlag::createEventFlag(eventQueue_->getEventFlagWord(), &eventQueueFlag_) != ::android::OK) {
        ALOGE("initialize(): EventFlag::createEventFlag failed");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    callback_ = in_sensorsCallback;

    ensureEnumeratedLocked();

    startWakeLockThreadLocked();
    startReaderLocked();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::activate(int32_t in_sensorHandle, bool in_enabled) {
    ensureEnumerated();
    std::lock_guard<std::mutex> lock(mutex_);
    if (gainByHandle_.find(in_sensorHandle) == gainByHandle_.end()) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
#if defined(MINDONE_HF_RUST_WIRE)
    const auto found = state_.find(in_sensorHandle);
    const SensorState st = found == state_.end() ? SensorState{} : found->second;
    const mindone_hf_state current{st.samplingPeriodNs, st.maxReportLatencyNs,
                                   st.active ? 1u : 0u, 0};
    mindone_hf_state_transition transition{};
    if (mindone_hf_state_activate(&current, in_sensorHandle, in_enabled ? 1u : 0u,
                                  &transition) != 0) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (!hf_.setEnable(static_cast<uint8_t>(in_sensorHandle), transition.next.active != 0,
                        transition.next.period_ns, transition.next.latency_ns)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    state_[in_sensorHandle] = {transition.next.active != 0, transition.next.period_ns,
                               transition.next.latency_ns};
#else
    SensorState& st = state_[in_sensorHandle];
    if (!hf_.setEnable(static_cast<uint8_t>(in_sensorHandle), in_enabled, st.samplingPeriodNs,
                        st.maxReportLatencyNs)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    st.active = in_enabled;
#endif
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                                     int64_t in_maxReportLatencyNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gainByHandle_.find(in_sensorHandle) == gainByHandle_.end()) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
#if defined(MINDONE_HF_RUST_WIRE)
    const auto found = state_.find(in_sensorHandle);
    const SensorState st = found == state_.end() ? SensorState{} : found->second;
    const mindone_hf_state current{st.samplingPeriodNs, st.maxReportLatencyNs,
                                   st.active ? 1u : 0u, 0};
    mindone_hf_state_transition transition{};
    if (mindone_hf_state_batch(&current, in_sensorHandle, in_samplingPeriodNs,
                               in_maxReportLatencyNs, &transition) != 0) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (transition.command == MINDONE_HF_STATE_ENABLE &&
        !hf_.setEnable(static_cast<uint8_t>(in_sensorHandle), transition.next.active != 0,
                        transition.next.period_ns, transition.next.latency_ns)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    state_[in_sensorHandle] = {transition.next.active != 0, transition.next.period_ns,
                               transition.next.latency_ns};
#else
    SensorState& st = state_[in_sensorHandle];
    if (st.active) {
        if (!hf_.setEnable(static_cast<uint8_t>(in_sensorHandle), true, in_samplingPeriodNs,
                            in_maxReportLatencyNs)) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
        }
    }
    st.samplingPeriodNs = in_samplingPeriodNs;
    st.maxReportLatencyNs = in_maxReportLatencyNs;
#endif
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::flush(int32_t in_sensorHandle) {
#if defined(MINDONE_HF_RUST_WIRE)
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = state_.find(in_sensorHandle);
    if (gainByHandle_.find(in_sensorHandle) == gainByHandle_.end() || it == state_.end()) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    bool oneShot = false;
    for (const auto& si : sensorList_) {
        if (si.sensorHandle == in_sensorHandle) {
            oneShot = (si.flags & SensorInfo::SENSOR_FLAG_BITS_MASK_REPORTING_MODE) ==
                    SensorInfo::SENSOR_FLAG_BITS_ONE_SHOT_MODE;
            break;
        }
    }
    const SensorState& st = it->second;
    const mindone_hf_state current{st.samplingPeriodNs, st.maxReportLatencyNs,
                                   st.active ? 1u : 0u, 0};
    mindone_hf_state_transition transition{};
    if (mindone_hf_state_flush(&current, in_sensorHandle, oneShot ? 1u : 0u, &transition) != 0) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (!hf_.requestFlush(static_cast<uint8_t>(in_sensorHandle))) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    return ::ndk::ScopedAStatus::ok();
#else
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& si : sensorList_) {
            if (si.sensorHandle == in_sensorHandle &&
                (si.flags & SensorInfo::SENSOR_FLAG_BITS_MASK_REPORTING_MODE) ==
                        SensorInfo::SENSOR_FLAG_BITS_ONE_SHOT_MODE) {
                return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = state_.find(in_sensorHandle);
    if (it == state_.end() || !it->second.active) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (!hf_.requestFlush(static_cast<uint8_t>(in_sensorHandle))) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    return ::ndk::ScopedAStatus::ok();
#endif
}

::ndk::ScopedAStatus Sensors::setOperationMode(ISensors::OperationMode in_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (in_mode != ISensors::OperationMode::NORMAL) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    mode_ = in_mode;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::injectSensorData(const Event& ) {
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus Sensors::registerDirectChannel(const ISensors::SharedMemInfo& ,
                                                     int32_t* _aidl_return) {
    *_aidl_return = -1;
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus Sensors::unregisterDirectChannel(int32_t ) {
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus Sensors::configDirectReport(int32_t ,
                                                  int32_t ,
                                                  ISensors::RateLevel ,
                                                  int32_t* _aidl_return) {
    *_aidl_return = -1;
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}
}
