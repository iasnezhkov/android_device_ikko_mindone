/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/IGnss.h>
#include <hidl/Status.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace mindone::gnss {

inline std::atomic<size_t> callbacksInFlight{0};

inline ndk::ScopedAStatus extensionError(
        int32_t code = ::aidl::android::hardware::gnss::IGnss::ERROR_GENERIC) {
    return ndk::ScopedAStatus::fromServiceSpecificError(code);
}

inline ndk::ScopedAStatus extensionStatus(const ::android::hardware::Return<void>& result) {
    return result.isOk() ? ndk::ScopedAStatus::ok() : extensionError();
}

inline ndk::ScopedAStatus extensionStatus(const ::android::hardware::Return<bool>& result) {
    return result.isOk() && static_cast<bool>(result) ? ndk::ScopedAStatus::ok() : extensionError();
}

template <typename T>
class CallbackSlot : public std::enable_shared_from_this<CallbackSlot<T>> {
  public:
    explicit CallbackSlot(std::shared_ptr<T> callback) : mCallback(std::move(callback)) {}
    std::shared_ptr<T> get() {
        std::lock_guard lock(mMutex);
        if (!mCallback)
            return nullptr;
        ++mInFlight;
        ++callbacksInFlight;
        return std::shared_ptr<T>(mCallback.get(),
                                  [target = mCallback, slot = this->shared_from_this()](T*) {
                                      static_cast<void>(target);
                                      std::lock_guard lock(slot->mMutex);
                                      --slot->mInFlight;
                                      --callbacksInFlight;
                                      slot->mDrained.notify_all();
                                  });
    }
    void invalidate() {
        std::unique_lock lock(mMutex);
        mCallback.reset();
        mDrained.wait(lock, [&] { return mInFlight == 0; });
    }

  private:
    std::mutex mMutex;
    std::condition_variable mDrained;
    size_t mInFlight = 0;
    std::shared_ptr<T> mCallback;
};

}
