/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone.sensors"

#include "Sensors.h"

#include <android/binder_status.h>
#include <log/log.h>
#include <utils/Errors.h>

#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace mindone {
namespace sensors {

using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::sensors::SensorType;
using ::mindone::hf::hf_manager_event;
using EventPayload = Event::EventPayload;

namespace {

// Bounded wait for HfManagerClient::waitUntilReady(). Task-specified upper
// bound; see SENSORS-HAL-PLAN-1309.md "readiness-wait design" for why this
// needs to comfortably clear the empirically measured ~27s SCP boot gap
// (F4290/F4292) with margin.
constexpr int kReadyTimeoutMs = 40000;

// EventFlag wake bit used by frameworks/native's FMQ-based sensors event
// reader to know a write happened. NOT independently re-derived here -
// copied from the value used across every AOSP sensors HAL generation
// (HIDL 2.x and AIDL). VERIFY against the actual framework build this ROM
// links (see plan doc, open risk "FMQ vs poll event delivery"): a mismatch
// degrades to the framework's own timeout-poll fallback rather than a
// crash, but should still be confirmed with a working accelerometer test.
constexpr uint32_t kEventQueueFlagBitsReadAndProcess = 1;

}  // namespace

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
        enumerated_ = true;  // nothing more we can do; leave sensorList_ empty
        return;
    }

    // THE fix for the boot race (/ F4290 / F4292): block here, before
    // any sensor_type is queried, until hf_manager reports the SCP-backed
    // sensor stack ready (or we give up after kReadyTimeoutMs). Both entry
    // points that can reach here (getSensorsList(), initialize()) call
    // this while already holding mutex_, so whichever the framework calls
    // first pays this wait and the other one returns instantly afterward.
    hf_.waitUntilReady(kReadyTimeoutMs);

    sensorList_.clear();
    gainByHandle_.clear();
    wakeUpByHandle_.clear();

    for (int t = 1; t < ::mindone::hf::SENSOR_TYPE_SENSOR_MAX; ++t) {
        auto typeInfo = ::mindone::hf::lookupSensorTypeInfo(static_cast<uint8_t>(t));
        if (!typeInfo) continue;  // MTK-only type or framework-synthesized meta type

        ::mindone::hf::sensor_info raw{};
        if (!hf_.querySensorInfo(static_cast<uint8_t>(t), &raw)) continue;  // not registered

        SensorInfo info{};
        info.sensorHandle = t;  // handle == kernel sensor_type, see Sensors.h
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
        gainByHandle_[t] = raw.gain ? raw.gain : 1;  // gain==0 would be a kernel bug; guard div-by-0
        wakeUpByHandle_[t] = (typeInfo->flags & ::mindone::hf::kFlagWakeUp) != 0;
        if (!typeInfo->verified) {
            ALOGW("sensor %s (type=%d handle=%d): SensorInfo numeric fields are "
                  "PLACEHOLDERS, not verified against the real chip - see plan doc",
                  info.name.c_str(), t, t);
        }
    }

    ALOGI("hf_manager enumeration done: %zu AIDL-mappable sensors found", sensorList_.size());

    // v2/calibration (deliverable A, SENSORS-HAL-CALIBRATION-1309.md):
    // push /mnt/vendor/nvcfg/sensor/*.json calibration into the SCP for
    // every registered, calibratable sensor_type (pushAll() queries
    // HfManagerClient::queryRegistered() itself, no need to track that
    // here), and subscribe to runtime BIAS_ACTION/CALI_ACTION/TEMP_ACTION
    // reports so translateEvent() below can persist updates back.
    // ensureEnumeratedLocked() only ever runs once per process (see the
    // enumerated_ guard above), so this pushes exactly once per HAL
    // lifetime, same as the stock blob does at its own startup.
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
    if (readerThread_.joinable()) readerThread_.join();
    stopWakeLockThreadLocked();
}

void Sensors::readerLoop() {
    std::vector<hf_manager_event> kevents;
    while (!stopReader_.load()) {
        /* The timeout exists only so this loop notices stopReader_; events themselves arrive
         * through the poll inside waitAndReadEvents and are not delayed by it. At 500 ms the
         * thread woke twice a second for the life of the HAL even with every sensor disabled,
         * which on a 1960 mAh device is pure cost. Five seconds keeps shutdown bounded and
         * costs a tenth of the wake-ups. */
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
    // ASSUMPTION (verify on-device): SCP timestamps reach userspace already
    // translated to AP CLOCK_BOOTTIME by the kernel's timesync module
    // (sensorhub/timesync.c), matching what Event.timestamp documents
    // (elapsedRealtimeNano timebase). If accel/gyro fusion looks jittery or
    // SensorService logs timestamp-in-the-past warnings, re-check this.
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
        // v2/calibration: route BIAS_ACTION/CALI_ACTION (and
        // TEMP_ACTION, once its persistence is confirmed) to
        // CalibrationStore so a runtime SCP recalibration gets written back
        // to the nvcfg JSON files, same as the stock HAL does (see
        // SENSORS-HAL-CALIBRATION-1309.md). onKernelEvent() itself ignores
        // any action/sensor_type it doesn't manage, so it's safe to call
        // unconditionally here. Still not surfaced on the AIDL event queue
        // either way: BIAS_ACTION/CALI_ACTION/TEMP_ACTION/TEST_ACTION/
        // RAW_ACTION remain internal MTK debug channels, not wired to the
        // AIDL surface - same v1 decision, unchanged in v2.
        calibrationStore_.onKernelEvent(ke);
        return std::nullopt;
    }

    auto gainIt = gainByHandle_.find(ke.sensor_type);
    const float gain = (gainIt != gainByHandle_.end() && gainIt->second != 0)
                                ? static_cast<float>(gainIt->second)
                                : 1.0f;
    // CONFIRMED by F4294 (the fact log, live /proc/hf_manager dump,
    // 13.09): physical = raw_word / gain (e.g. icm4n607_acc gain=1000,
    // icm4n607_gyro gain=1000000, mmc5603 mag gain=1000, stk6a2x_als
    // gain=1). Still worth a one-time sanity check with the accelerometer
    // lying flat (expect ~9.81 on one axis) since F4294 read gain via
    // /proc/hf_manager's debug text dump, not through this exact ioctl
    // path - see plan doc open risks.
    auto scaled = [&](int idx) { return static_cast<float>(ke.word[idx]) / gain; };
    // SensorStatus is referenced here as a top-level android.hardware.sensors
    // type; if this AIDL version instead nests it under
    // Event::EventPayload::Vec3::SensorStatus, this is a one-line fix.
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
            // x, y, z, w, estimated_accuracy_radians (historical 5-value
            // rotation vector layout); Data.values is float[16], rest 0.
            for (int i = 0; i < 5; ++i) data.values[i] = scaled(i);
            e.payload.set<EventPayload::data>(data);
            break;
        }
        case SensorType::STEP_COUNTER:
            e.payload.set<EventPayload::stepCount>(static_cast<int64_t>(ke.word[0]));
            break;
        default:
            // LIGHT, PRESSURE, PROXIMITY, TEMPERATURE, AMBIENT_TEMPERATURE,
            // RELATIVE_HUMIDITY, DEVICE_ORIENTATION, and every one-shot/
            // special-reporting gesture/detector: single scalar payload
            // per Event.aidl doc comments.
            e.payload.set<EventPayload::scalar>(scaled(0));
            break;
    }
    return e;
}

void Sensors::postEvents(const std::vector<Event>& events) {
    std::lock_guard<std::mutex> lock(writeLock_);
    if (!eventQueue_) return;  // initialize() not called yet, drop
    if (eventQueue_->write(events.data(), events.size())) {
        if (eventQueueFlag_ == nullptr) {
            // Cannot wake the receiver: do not take a wake lock we could never release.
            return;
        }
        eventQueueFlag_->wake(kEventQueueFlagBitsReadAndProcess);
        int32_t wakeUp = 0;
        for (const auto& e : events) {
            auto it = wakeUpByHandle_.find(e.sensorHandle);
            if (it != wakeUpByHandle_.end() && it->second) ++wakeUp;
        }
        if (wakeUp > 0) updateWakeLock(wakeUp, 0 /* eventsHandled */);
    } else {
        ALOGE("hf_manager->AIDL: dropped %zu events, event FMQ full", events.size());
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
    /* The short timeout is what bounds how quickly a held wake lock is auto-released, so it is
     * only worth paying while one is actually held. With nothing held there is nothing to
     * release and the wake-up buys nothing - it just woke the CPU twice a second forever. Poll
     * fast only when it matters; a framework write to the queue still returns immediately either
     * way, because readBlocking wakes on the data-written flag rather than on the timeout. */
    constexpr int64_t kHeldTimeoutNs = 500LL * 1000 * 1000;   // 500 ms while a lock is held
    constexpr int64_t kIdleTimeoutNs = 5000LL * 1000 * 1000;  // 5 s when there is nothing to release

    while (readWakeLockQueueRun_.load()) {
        const int64_t timeoutNs = hasWakeLock_ ? kHeldTimeoutNs : kIdleTimeoutNs;
        int32_t eventsHandled = 0;
        if (wakeLockQueue_) {
            wakeLockQueue_->readBlocking(
                    &eventsHandled, 1 /* count */, 0 /* readNotification */,
                    static_cast<uint32_t>(ISensors::WAKE_LOCK_QUEUE_FLAG_BITS_DATA_WRITTEN),
                    timeoutNs);
        } else {
            usleep(static_cast<useconds_t>(timeoutNs / 1000));
        }
        updateWakeLock(0 /* eventsWritten */, eventsHandled);
    }
}

void Sensors::updateWakeLock(int32_t eventsWritten, int32_t eventsHandled) {
    std::lock_guard<std::mutex> lock(wakeLockLock_);
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

// ---------------------------------------------------------------------
// ISensors
// ---------------------------------------------------------------------

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

    // Defensive: also gate here in case a future framework build calls
    // initialize() before getSensorsList(). See ensureEnumeratedLocked().
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
    SensorState& st = state_[in_sensorHandle];
    if (!hf_.setEnable(static_cast<uint8_t>(in_sensorHandle), in_enabled, st.samplingPeriodNs,
                        st.maxReportLatencyNs)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
    }
    st.active = in_enabled;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                                     int64_t in_maxReportLatencyNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gainByHandle_.find(in_sensorHandle) == gainByHandle_.end()) {
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    SensorState& st = state_[in_sensorHandle];
    st.samplingPeriodNs = in_samplingPeriodNs;
    st.maxReportLatencyNs = in_maxReportLatencyNs;
    if (st.active) {
        // hf_manager has no separate "change batch params while active"
        // primitive: action==ENABLE always carries the current delay/
        // latency and the kernel treats re-sending it as an update
        // (hf_manager_save_update_enable(), hf_manager.c:652-689). So
        // applying a new batch to an already-active sensor means
        // re-issuing ENABLE, not a distinct ioctl.
        if (!hf_.setEnable(static_cast<uint8_t>(in_sensorHandle), true, st.samplingPeriodNs,
                            st.maxReportLatencyNs)) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(EIO);
        }
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::flush(int32_t in_sensorHandle) {
    // ISensors.aidl: flush() on a one-shot sensor must fail with EX_ILLEGAL_ARGUMENT (the framework
    // already filters this, a direct HAL client is not protected otherwise).
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
    // Completion arrives asynchronously as a META_DATA/META_DATA_FLUSH_
    // COMPLETE event on the FMQ once the kernel replays a FLUSH_ACTION
    // record for this sensor_type - see translateEvent().
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::setOperationMode(ISensors::OperationMode in_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (in_mode != ISensors::OperationMode::NORMAL) {
        // DATA_INJECTION unsupported in v1 - see plan doc.
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    mode_ = in_mode;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Sensors::injectSensorData(const Event& /*in_event*/) {
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus Sensors::registerDirectChannel(const ISensors::SharedMemInfo& /*in_mem*/,
                                                     int32_t* _aidl_return) {
    *_aidl_return = -1;
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus Sensors::unregisterDirectChannel(int32_t /*in_channelHandle*/) {
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus Sensors::configDirectReport(int32_t /*in_sensorHandle*/,
                                                  int32_t /*in_channelHandle*/,
                                                  ISensors::RateLevel /*in_rate*/,
                                                  int32_t* _aidl_return) {
    *_aidl_return = -1;
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace sensors
}  // namespace mindone
