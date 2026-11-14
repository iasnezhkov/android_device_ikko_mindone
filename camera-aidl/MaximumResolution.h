/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/hardware/camera/provider/2.6/ICameraProvider.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace mindone::camera {

struct MaximumResolution {
    uint32_t remosaicTag = 0;
    bool enabled() const { return remosaicTag != 0; }
};

int32_t applyMaximumResolutionSwitch();
MaximumResolution loadMaximumResolution(
        int32_t mode,
        const ::android::sp<::android::hardware::camera::provider::V2_6::ICameraProvider>& backend);

class PixelModes {
  public:
    void configure(uint32_t remosaicTag, int32_t binning, int32_t partialCount);
    bool enabled();
    bool mapRequests(const std::vector<uint32_t>& frames,
                     std::vector<std::vector<uint8_t>>* settings);
    void keep(const std::vector<uint32_t>& frames, size_t processed);
    bool mapResult(uint32_t frame, uint32_t partial, std::vector<uint8_t>* result);
    void forget(uint32_t frame);
    void clear();

  private:
    std::mutex mMutex;
    uint32_t mRemosaicTag = 0;
    int32_t mBinning = 0;
    uint32_t mPartialCount = 0;
    uint8_t mLast = 0;
    std::vector<uint8_t> mBatch;
    std::map<uint32_t, uint8_t> mFrames;
};

}
