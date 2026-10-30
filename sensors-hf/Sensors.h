/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
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
        int64_t samplingPeriodNs = 200000000;
        int64_t maxReportLatencyNs = 0;
    };

    void ensureEnumerated();

    void ensureEnumeratedLocked();

    void startReaderLocked();
    void stopReaderLocked();
    void readerLoop();

    std::optional<Event> translateEvent(const ::mindone::hf::hf_manager_event& ke);
    void postEvents(const std::vector<Event>& events);

    ::mindone::hf::HfManagerClient hf_;

    ::mindone::hf::CalibrationStore calibrationStore_;

    std::mutex mutex_;
    bool enumerated_ = false;
    std::vector<SensorInfo> sensorList_;
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

    static constexpr const char* kWakeLockName = "SensorsHAL_WAKEUP";
    std::unordered_map<int32_t, bool> wakeUpByHandle_;
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
    void updateWakeLockLocked(int32_t eventsWritten, int32_t eventsHandled);
};

}
}

#endif
