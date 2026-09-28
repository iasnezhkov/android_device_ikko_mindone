#pragma once

#include <android/hardware/camera/provider/2.4/ICameraProviderCallback.h>
#include <android/hardware/camera/provider/2.6/ICameraProvider.h>

#include <functional>
#include <mutex>

namespace mindone {
namespace camera {

class ProviderDeathRecipient;

using ProviderStateCallback = std::function<void(bool available)>;

class CameraProviderHidlConn {
  public:
    CameraProviderHidlConn();
    ~CameraProviderHidlConn();

    bool connect();

    ::android::sp<::android::hardware::camera::provider::V2_6::ICameraProvider> get() const;

    ::android::hardware::Return<::android::hardware::camera::common::V1_0::Status> setCallback(
            const ::android::sp<::android::hardware::camera::provider::V2_4::ICameraProviderCallback>&
                    callback);

    void setStateCallback(ProviderStateCallback cb);

  private:
    friend class ProviderDeathRecipient;
    void onServiceDied();

    mutable std::mutex mLock;
    ::android::sp<::android::hardware::camera::provider::V2_6::ICameraProvider> mHidl;
    ::android::sp<::android::hardware::camera::provider::V2_4::ICameraProviderCallback> mCallback;
    ::android::sp<ProviderDeathRecipient> mDeathRecipient;
    ProviderStateCallback mCb;
};

}
}
