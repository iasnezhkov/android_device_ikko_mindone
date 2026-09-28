#include "CameraDeviceProxy35.h"

#include "MetadataAugmenter.h"

namespace mindone {
namespace camera {

using ::android::hardware::Return;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::TorchMode;
using ::android::hardware::camera::device::V3_2::CameraMetadata;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;
using ::android::hardware::camera::device::V3_4::StreamConfiguration;
using ::android::hardware::camera::device::V3_5::ICameraDevice;

CameraDeviceProxy35::CameraDeviceProxy35(::android::sp<ICameraDevice> real) : mReal(std::move(real)) {}

Return<void> CameraDeviceProxy35::getResourceCost(getResourceCost_cb _hidl_cb) {
    return mReal->getResourceCost(_hidl_cb);
}

Return<void> CameraDeviceProxy35::getCameraCharacteristics(getCameraCharacteristics_cb _hidl_cb) {
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

Return<Status> CameraDeviceProxy35::setTorchMode(TorchMode mode) {
    return mReal->setTorchMode(mode);
}

Return<void> CameraDeviceProxy35::open(const ::android::sp<ICameraDeviceCallback>& callback,
        open_cb _hidl_cb) {
    return mReal->open(callback, _hidl_cb);
}

Return<void> CameraDeviceProxy35::dumpState(const ::android::hardware::hidl_handle& fd) {
    return mReal->dumpState(fd);
}

Return<void> CameraDeviceProxy35::getPhysicalCameraCharacteristics(
        const ::android::hardware::hidl_string& physicalCameraId,
        getPhysicalCameraCharacteristics_cb _hidl_cb) {
    return mReal->getPhysicalCameraCharacteristics(physicalCameraId, _hidl_cb);
}

Return<void> CameraDeviceProxy35::isStreamCombinationSupported(const StreamConfiguration& streams,
        isStreamCombinationSupported_cb _hidl_cb) {
    return mReal->isStreamCombinationSupported(streams, _hidl_cb);
}

}
}
