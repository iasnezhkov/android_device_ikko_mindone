/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * android.hardware.sensors ISensors/default implementation for the iKKO
 * MindOne (MT6789), backed directly by the mind_one kernel's hf_manager
 * driver (/dev/hf_manager) instead of the stock, unpatchable
 * sensors.mt6789.so + HIDL 2.0 multihal chain it replaces.
 *
 * Modeled on the AOSP reference implementation's shape
 * (hardware/interfaces/sensors/aidl/default/{include/sensors-impl/Sensors.h,
 * Sensors.cpp}) but NOT copied from it verbatim: that reference generates
 * simulated per-sensor data on independent per-Sensor threads, whereas we
 * have exactly one real kernel event stream (one fd, one FIFO shared by
 * every enabled sensor - hf_manager.c:89 struct hf_client_fifo) so this
 * class owns a single reader thread instead of one thread per Sensor
 * object. See SENSORS-HAL-PLAN-1309.md for the full design rationale and
 * the kernel ABI citations behind every ioctl/write() call here.
 */

#ifndef MINDONE_SENSORS_H_
#define MINDONE_SENSORS_H_

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include <aidl/android/hardware/common/fmq/MQDescriptor.h>
#include <aidl/android/hardware/common/fmq/SynchronizedReadWrite.h>
#include <aidl/android/hardware/sensors/BnSensors.h>
#include <fmq/AidlMessageQueue.h>
#include <fmq/EventFlag.h>
#include <hardware_legacy/power.h>
#include <utils/SystemClock.h>

#include "CalibrationStore.h"
#include "HfManagerClient.h"
#include "SensorTypeMap.h"

namespace mindone {
namespace sensors {

using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::aidl::android::hardware::sensors::BnSensors;
using ::aidl::android::hardware::sensors::Event;
using ::aidl::android::hardware::sensors::ISensors;
using ::aidl::android::hardware::sensors::ISensorsCallback;
using ::aidl::android::hardware::sensors::SensorInfo;
using ::android::AidlMessageQueue;
using ::android::hardware::EventFlag;

class Sensors : public BnSensors {
  public:
    Sensors();
    ~Sensors() override;

    // ISensors
    ::ndk::ScopedAStatus activate(int32_t in_sensorHandle, bool in_enabled) override;
    ::ndk::ScopedAStatus batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                                int64_t in_maxReportLatencyNs) override;
    ::ndk::ScopedAStatus configDirectReport(int32_t in_sensorHandle, int32_t in_channelHandle,
                                             ISensors::RateLevel in_rate,
                                             int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus flush(int32_t in_sensorHandle) override;
    ::ndk::ScopedAStatus getSensorsList(std::vector<SensorInfo>* _aidl_return) override;
    ::ndk::ScopedAStatus initialize(
            const ::aidl::android::hardware::common::fmq::MQDescriptor<
                    Event, SynchronizedReadWrite>& in_eventQueueDescriptor,
            const ::aidl::android::hardware::common::fmq::MQDescriptor<
                    int32_t, SynchronizedReadWrite>& in_wakeLockDescriptor,
            const std::shared_ptr<ISensorsCallback>& in_sensorsCallback) override;
    ::ndk::ScopedAStatus injectSensorData(const Event& in_event) override;
    ::ndk::ScopedAStatus registerDirectChannel(const ISensors::SharedMemInfo& in_mem,
                                                int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus setOperationMode(ISensors::OperationMode in_mode) override;
    ::ndk::ScopedAStatus unregisterDirectChannel(int32_t in_channelHandle) override;

  private:
    struct SensorState {
        bool active = false;
        int64_t samplingPeriodNs = 200000000;  // 200ms default until batch() sets one
        int64_t maxReportLatencyNs = 0;
    };

    // Opens /dev/hf_manager (if not already) and, on the FIRST call only,
    // runs HfManagerClient::waitUntilReady() (bounded, ~40s) followed by
    // enumeration over every kernel sensor_type via
    // HF_MANAGER_REQUEST_SENSOR_INFO, building sensorList_/gainByHandle_.
    // Safe to call from both getSensorsList() and initialize() - whichever
    // runs first pays the wait, the other is then a no-op. See plan doc
    // for why both entry points are guarded rather than trusting a single
    // assumed call order from the framework.
    void ensureEnumerated();

    // Body of ensureEnumerated(), caller must hold mutex_.
    void ensureEnumeratedLocked();

    void startReaderLocked();
    void stopReaderLocked();
    void readerLoop();

    // Translates one raw kernel event into zero or one AIDL Event. Returns
    // nullopt for: unmapped MTK-only sensor_type, and kernel actions with
    // no v1 AIDL surface (BIAS/CALI/TEMP/TEST/RAW_ACTION).
    std::optional<Event> translateEvent(const ::mindone::hf::hf_manager_event& ke);
    void postEvents(const std::vector<Event>& events);

    ::mindone::hf::HfManagerClient hf_;

    // v2/calibration (SENSORS-HAL-CALIBRATION-1309.md): pushes nvcfg
    // JSON calibration through hf_ once per ensureEnumeratedLocked() run,
    // and persists runtime BIAS_ACTION/CALI_ACTION results seen in
    // translateEvent() back to disk. Declared right after hf_ since the two
    // are always used together; CalibrationStore never touches hf_'s fd
    // itself, only the HfManagerClient& handed to pushAll().
    ::mindone::hf::CalibrationStore calibrationStore_;

    std::mutex mutex_;
    bool enumerated_ = false;
    std::vector<SensorInfo> sensorList_;
    // handle == kernel sensor_type in this implementation (both already
    // unique, positive, and stable for the process lifetime), so this map
    // only needs to carry what SensorInfo itself doesn't: the raw->physical
    // scale factor and per-handle activation/batch state.
    std::unordered_map<int32_t, uint32_t> gainByHandle_;
    std::unordered_map<int32_t, SensorState> state_;
    ISensors::OperationMode mode_ = ISensors::OperationMode::NORMAL;

    std::shared_ptr<ISensorsCallback> callback_;
    std::unique_ptr<AidlMessageQueue<Event, SynchronizedReadWrite>> eventQueue_;
    std::unique_ptr<AidlMessageQueue<int32_t, SynchronizedReadWrite>> wakeLockQueue_;
    EventFlag* eventQueueFlag_ = nullptr;
    std::mutex writeLock_;

    std::thread readerThread_;
    std::atomic<bool> stopReader_{false};

    // Wake-up sensor protocol (android.hardware.sensors ISensors contract): while WAKE_UP events
    // written to the event FMQ have not been acknowledged by the framework through the wake-lock
    // FMQ, the HAL holds a partial wake lock; it is released when the outstanding count returns to
    // zero or after WAKE_LOCK_TIMEOUT_SECONDS without acknowledgements (auto-release safety net).
    // Same shape as the AOSP reference HAL (hardware/interfaces/sensors/aidl/default).
    static constexpr const char* kWakeLockName = "SensorsHAL_WAKEUP";
    std::unordered_map<int32_t, bool> wakeUpByHandle_;  // from SensorInfo.flags WAKE_UP bit
    std::thread wakeLockThread_;
    std::atomic<bool> readWakeLockQueueRun_{false};
    std::mutex wakeLockLock_;
    int32_t outstandingWakeUpEvents_ = 0;
    int64_t autoReleaseWakeLockTime_ = 0;
    bool hasWakeLock_ = false;
    void startWakeLockThreadLocked();
    void stopWakeLockThreadLocked();
    void readWakeLockFMQ();
    void updateWakeLock(int32_t eventsWritten, int32_t eventsHandled);
};

}  // namespace sensors
}  // namespace mindone

#endif  // MINDONE_SENSORS_H_
