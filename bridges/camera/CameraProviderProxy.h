#pragma once

#include <android/hardware/camera/provider/2.6/ICameraProvider.h>

#include "CameraProviderHidlConn.h"

namespace mindone {
namespace camera {

class CameraProviderProxy : public ::android::hardware::camera::provider::V2_6::ICameraProvider {
  public:
    explicit CameraProviderProxy(CameraProviderHidlConn* conn);

    ::android::hardware::Return<::android::hardware::camera::common::V1_0::Status> setCallback(
            const ::android::sp<
                    ::android::hardware::camera::provider::V2_4::ICameraProviderCallback>& callback)
            override;

    ::android::hardware::Return<void> getVendorTags(getVendorTags_cb _hidl_cb) override;

    ::android::hardware::Return<void> getCameraIdList(getCameraIdList_cb _hidl_cb) override;

    ::android::hardware::Return<void> isSetTorchModeSupported(
            isSetTorchModeSupported_cb _hidl_cb) override;

    ::android::hardware::Return<void> getCameraDeviceInterface_V1_x(
            const ::android::hardware::hidl_string& cameraDeviceName,
            getCameraDeviceInterface_V1_x_cb _hidl_cb) override;

    ::android::hardware::Return<void> getCameraDeviceInterface_V3_x(
            const ::android::hardware::hidl_string& cameraDeviceName,
            getCameraDeviceInterface_V3_x_cb _hidl_cb) override;

    ::android::hardware::Return<void> notifyDeviceStateChange(
            ::android::hardware::hidl_bitfield<
                    ::android::hardware::camera::provider::V2_5::DeviceState> newState) override;

    ::android::hardware::Return<void> getConcurrentStreamingCameraIds(
            getConcurrentStreamingCameraIds_cb _hidl_cb) override;

    ::android::hardware::Return<void> isConcurrentStreamCombinationSupported(
            const ::android::hardware::hidl_vec<
                    ::android::hardware::camera::provider::V2_6::CameraIdAndStreamCombination>&
                    configs,
            isConcurrentStreamCombinationSupported_cb _hidl_cb) override;

  private:
    CameraProviderHidlConn* mConn;
};

}
}
