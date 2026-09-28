#pragma once

#include <android/hardware/camera/device/3.5/ICameraDevice.h>

namespace mindone {
namespace camera {

class CameraDeviceProxy35 : public ::android::hardware::camera::device::V3_5::ICameraDevice {
  public:
    explicit CameraDeviceProxy35(
            ::android::sp<::android::hardware::camera::device::V3_5::ICameraDevice> real);

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

    ::android::hardware::Return<void> getPhysicalCameraCharacteristics(
            const ::android::hardware::hidl_string& physicalCameraId,
            getPhysicalCameraCharacteristics_cb _hidl_cb) override;

    ::android::hardware::Return<void> isStreamCombinationSupported(
            const ::android::hardware::camera::device::V3_4::StreamConfiguration& streams,
            isStreamCombinationSupported_cb _hidl_cb) override;

  private:
    ::android::sp<::android::hardware::camera::device::V3_5::ICameraDevice> mReal;
};

}
}
