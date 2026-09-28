#include "CameraDeviceProxy.h"

#include "MetadataAugmenter.h"

namespace mindone {
namespace camera {

using ::android::hardware::Return;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::TorchMode;
using ::android::hardware::camera::device::V3_2::CameraMetadata;
using ::android::hardware::camera::device::V3_2::ICameraDevice;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;

CameraDeviceProxy::CameraDeviceProxy(::android::sp<ICameraDevice> real) : mReal(std::move(real)) {}

Return<void> CameraDeviceProxy::getResourceCost(getResourceCost_cb _hidl_cb) {
    return mReal->getResourceCost(_hidl_cb);
}

Return<void> CameraDeviceProxy::getCameraCharacteristics(getCameraCharacteristics_cb _hidl_cb) {
    return mReal->getCameraCharacteristics([&](Status status, CameraMetadata metadata) {
        if (status == Status::OK) {
            augmentHdrSessionKey(&metadata);
            if (kEnableMaximumResolutionAugment) {
                augmentMaximumResolution(&metadata);
            }
        }
        _hidl_cb(status, metadata);
    });
}

Return<Status> CameraDeviceProxy::setTorchMode(TorchMode mode) {
    return mReal->setTorchMode(mode);
}

Return<void> CameraDeviceProxy::open(const ::android::sp<ICameraDeviceCallback>& callback,
        open_cb _hidl_cb) {
    return mReal->open(callback, _hidl_cb);
}

Return<void> CameraDeviceProxy::dumpState(const ::android::hardware::hidl_handle& fd) {
    return mReal->dumpState(fd);
}

}
}
