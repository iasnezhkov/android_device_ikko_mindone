#include "CameraProviderProxy.h"

#include "CameraDeviceProxy.h"
#include "CameraDeviceProxy35.h"

#include <android/hardware/camera/device/3.5/ICameraDevice.h>
#include <log/log.h>

namespace mindone {
namespace camera {

using ::android::sp;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::device::V3_2::ICameraDevice;
using ::android::hardware::camera::provider::V2_4::ICameraProviderCallback;
using ::android::hardware::camera::provider::V2_6::CameraIdAndStreamCombination;
using ::android::hardware::camera::provider::V2_6::ICameraProvider;

CameraProviderProxy::CameraProviderProxy(CameraProviderHidlConn* conn) : mConn(conn) {}

Return<Status> CameraProviderProxy::setCallback(const sp<ICameraProviderCallback>& callback) {
    return mConn->setCallback(callback);
}

Return<void> CameraProviderProxy::getVendorTags(getVendorTags_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, {});
        return ::android::hardware::Void();
    }
    return real->getVendorTags(_hidl_cb);
}

Return<void> CameraProviderProxy::getCameraIdList(getCameraIdList_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, {});
        return ::android::hardware::Void();
    }
    return real->getCameraIdList(_hidl_cb);
}

Return<void> CameraProviderProxy::isSetTorchModeSupported(isSetTorchModeSupported_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, false);
        return ::android::hardware::Void();
    }
    return real->isSetTorchModeSupported(_hidl_cb);
}

Return<void> CameraProviderProxy::getCameraDeviceInterface_V1_x(const hidl_string& cameraDeviceName,
        getCameraDeviceInterface_V1_x_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, nullptr);
        return ::android::hardware::Void();
    }
    return real->getCameraDeviceInterface_V1_x(cameraDeviceName, _hidl_cb);
}

Return<void> CameraProviderProxy::getCameraDeviceInterface_V3_x(const hidl_string& cameraDeviceName,
        getCameraDeviceInterface_V3_x_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, nullptr);
        return ::android::hardware::Void();
    }
    return real->getCameraDeviceInterface_V3_x(cameraDeviceName,
            [&](Status status, const sp<ICameraDevice>& device) {
                if (status != Status::OK || device == nullptr) {
                    _hidl_cb(status, device);
                    return;
                }
                auto castResult = ::android::hardware::camera::device::V3_5::ICameraDevice::castFrom(device);
                sp<::android::hardware::camera::device::V3_5::ICameraDevice> device35 = castResult;
                if (device35 != nullptr) {
                    _hidl_cb(status, new CameraDeviceProxy35(device35));
                } else {
                    _hidl_cb(status, new CameraDeviceProxy(device));
                }
            });
}

Return<void> CameraProviderProxy::notifyDeviceStateChange(
        ::android::hardware::hidl_bitfield<::android::hardware::camera::provider::V2_5::DeviceState>
                newState) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        return ::android::hardware::Void();
    }
    return real->notifyDeviceStateChange(newState);
}

Return<void> CameraProviderProxy::getConcurrentStreamingCameraIds(
        getConcurrentStreamingCameraIds_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, {});
        return ::android::hardware::Void();
    }
    return real->getConcurrentStreamingCameraIds(_hidl_cb);
}

Return<void> CameraProviderProxy::isConcurrentStreamCombinationSupported(
        const hidl_vec<CameraIdAndStreamCombination>& configs,
        isConcurrentStreamCombinationSupported_cb _hidl_cb) {
    sp<ICameraProvider> real = mConn->get();
    if (real == nullptr) {
        _hidl_cb(Status::INTERNAL_ERROR, false);
        return ::android::hardware::Void();
    }
    return real->isConcurrentStreamCombinationSupported(configs, _hidl_cb);
}

}
}
