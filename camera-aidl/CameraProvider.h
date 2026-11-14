/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "MaximumResolution.h"

#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <android/hardware/camera/provider/2.6/ICameraProvider.h>

#include <memory>
#include <string>
#include <vector>

namespace mindone::camera {

namespace ap = ::aidl::android::hardware::camera::provider;
namespace ac = ::aidl::android::hardware::camera::common;
namespace ad = ::aidl::android::hardware::camera::device;
namespace hp = ::android::hardware::camera::provider;
class FlipWatcher;
class NameMap;
class SensorOwner;
struct FlipLinks;

class CameraProvider : public ap::BnCameraProvider {
  public:
    CameraProvider(const ::android::sp<hp::V2_6::ICameraProvider>& backend,
                   const MaximumResolution& maximumResolution);
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<ap::ICameraProviderCallback>& callback) override;
    ndk::ScopedAStatus getVendorTags(std::vector<ac::VendorTagSection>* tags) override;
    ndk::ScopedAStatus getCameraIdList(std::vector<std::string>* names) override;
    ndk::ScopedAStatus getCameraDeviceInterface(
            const std::string& name, std::shared_ptr<ad::ICameraDevice>* device) override;
    ndk::ScopedAStatus notifyDeviceStateChange(int64_t state) override;
    ndk::ScopedAStatus getConcurrentCameraIds(
            std::vector<ap::ConcurrentCameraIdCombination>* combinations) override;
    ndk::ScopedAStatus isConcurrentStreamCombinationSupported(
            const std::vector<ap::CameraIdAndStreamCombination>& configs, bool* supported) override;

  private:
    const ::android::sp<hp::V2_6::ICameraProvider> mBackend;
    const std::shared_ptr<NameMap> mNames;
    const std::shared_ptr<SensorOwner> mSensor;
    const std::shared_ptr<FlipLinks> mLinks;
    const std::shared_ptr<FlipWatcher> mFlip;
    const MaximumResolution mMaximumResolution;
};

}
