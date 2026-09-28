#include "CameraProviderHidlConn.h"

#include "Instances.h"

#include <hidl/HidlSupport.h>
#include <log/log.h>

#include <thread>

namespace mindone {
namespace camera {

using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::provider::V2_4::ICameraProviderCallback;
using ::android::hardware::camera::provider::V2_6::ICameraProvider;
using ::android::hardware::hidl_death_recipient;

class ProviderDeathRecipient : public hidl_death_recipient {
  public:
    explicit ProviderDeathRecipient(CameraProviderHidlConn* owner) : mOwner(owner) {}

    void serviceDied(uint64_t, const ::android::wp<::android::hidl::base::V1_0::IBase>&) override {
        mOwner->onServiceDied();
    }

  private:
    CameraProviderHidlConn* mOwner;
};

CameraProviderHidlConn::CameraProviderHidlConn() : mDeathRecipient(new ProviderDeathRecipient(this)) {}

CameraProviderHidlConn::~CameraProviderHidlConn() {
    std::lock_guard<std::mutex> lock(mLock);
    if (mHidl != nullptr) {
        mHidl->unlinkToDeath(mDeathRecipient);
    }
}

bool CameraProviderHidlConn::connect() {
    ::android::sp<ICameraProvider> real = ICameraProvider::getService(kRealProviderInstance);
    if (real == nullptr) {
        ALOGE("mindone.camera: real provider %s did not register", kRealProviderInstance);
        return false;
    }
    real->linkToDeath(mDeathRecipient, 0);
    sp<ICameraProviderCallback> callback;
    ProviderStateCallback cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mHidl = real;
        callback = mCallback;
        cb = mCb;
    }
    if (callback != nullptr) {
        real->setCallback(callback);
    }
    if (cb) {
        cb(true);
    }
    return true;
}

::android::sp<ICameraProvider> CameraProviderHidlConn::get() const {
    std::lock_guard<std::mutex> lock(mLock);
    return mHidl;
}

Return<Status> CameraProviderHidlConn::setCallback(const sp<ICameraProviderCallback>& callback) {
    sp<ICameraProvider> real;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mCallback = callback;
        real = mHidl;
    }
    if (real == nullptr) {
        return Status::INTERNAL_ERROR;
    }
    return real->setCallback(callback);
}

void CameraProviderHidlConn::setStateCallback(ProviderStateCallback cb) {
    std::lock_guard<std::mutex> lock(mLock);
    mCb = std::move(cb);
}

void CameraProviderHidlConn::onServiceDied() {
    ProviderStateCallback cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mHidl = nullptr;
        cb = mCb;
    }
    if (cb) {
        cb(false);
    }
    std::thread([this]() {
        while (!connect()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }).detach();
}

}
}
