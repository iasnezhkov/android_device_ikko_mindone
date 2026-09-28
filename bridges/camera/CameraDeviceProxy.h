#pragma once

#include <android/hardware/camera/device/3.2/ICameraDevice.h>

namespace mindone {
namespace camera {

class CameraDeviceProxy : public ::android::hardware::camera::device::V3_2::ICameraDevice {
  public:
    explicit CameraDeviceProxy(
            ::android::sp<::android::hardware::camera::device::V3_2::ICameraDevice> real);

    ::android::hardware::Return<void> getResourceCost(getResourceCost_cb _hidl_cb) override;

    ::android::hardware::Return<void> getCameraCharacteristics(
            getCameraCharacteristics_cb _hidl_cb) override;

    ::android::hardware::Return<::android::hardware::camera::common::V1_0::Status> setTorchMode(
            ::android::hardware::camera::common::V1_0::TorchMode mode) override;

    ::android::hardware::Return<void> open(
            const ::android::sp<::android::hardware::camera::device::V3_2::ICameraDeviceCallback>&
                    callback,
            open_cb _hidl_cb) override;

    ::android::hardware::Return<void> dumpState(const ::android::hardware::hidl_handle& fd) override;

  private:
    ::android::sp<::android::hardware::camera::device::V3_2::ICameraDevice> mReal;
};

}
}
