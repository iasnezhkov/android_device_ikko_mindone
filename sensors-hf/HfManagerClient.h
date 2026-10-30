/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINDONE_HF_MANAGER_CLIENT_H_
#define MINDONE_HF_MANAGER_CLIENT_H_

#include <cstdint>
#include <vector>

#include "hf_manager_uapi.h"

namespace mindone {
namespace hf {

class HfManagerClient {
  public:
    HfManagerClient() = default;
    ~HfManagerClient();

    HfManagerClient(const HfManagerClient&) = delete;
    HfManagerClient& operator=(const HfManagerClient&) = delete;

    bool open();
    bool isOpen() const { return fd_ >= 0; }
    int fd() const { return fd_; }

    bool queryReady();

    bool queryRegistered(uint8_t sensorType);

    bool querySensorInfo(uint8_t sensorType, sensor_info* out);

    bool waitUntilReady(int timeoutMs, int pollIntervalMs = 300);

    int countRegisteredSensors();

    bool setEnable(uint8_t sensorType, bool enabled, int64_t delayNs, int64_t latencyNs);

    bool requestFlush(uint8_t sensorType);

    bool configCalibration(uint8_t sensorType, const void* data, uint8_t length);

    bool requestBiasReports(uint8_t sensorType, bool enable);
    bool requestCaliReports(uint8_t sensorType, bool enable);
    bool requestTempReports(uint8_t sensorType, bool enable);

    bool waitAndReadEvents(int timeoutMs, std::vector<hf_manager_event>* out);

  private:
    int fd_ = -1;
};

}
}

#endif
