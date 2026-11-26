/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/camera/device/RequestTemplate.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace mindone::camera {

bool oisAvailable();

class OisModes {
  public:
    void configure(int32_t partialCount);
    bool enabled();
    bool setDefaultRequest(::aidl::android::hardware::camera::device::RequestTemplate type,
                           std::vector<uint8_t>* bytes);
    bool mapRequests(const std::vector<uint32_t>& frames,
                     std::vector<std::vector<uint8_t>>* settings);
    void keep(const std::vector<uint32_t>& frames, size_t processed);
    bool mapResult(uint32_t frame, uint32_t partial, std::vector<uint8_t>* result);
    void forget(uint32_t frame);
    void park();
    void clear();

  private:
    void applyLocked(const std::string& mode);

    std::mutex mMutex;
    bool mEnabled = false;
    uint32_t mPartialCount = 0;
    uint8_t mLast = 0;
    std::string mApplied;
    std::vector<uint8_t> mBatch;
    std::map<uint32_t, uint8_t> mFrames;
};

}
