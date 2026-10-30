/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-fingerprint"

#include "FingerprintHidlConn.h"
#include "LegacyFingerprint.h"

#include <android-base/logging.h>
#include <utility>

namespace mindone {

using ::android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint;

FingerprintHidlConn::FingerprintHidlConn() = default;
FingerprintHidlConn::~FingerprintHidlConn() = default;

bool FingerprintHidlConn::connect() {
    std::lock_guard<std::mutex> lock(mLock);
    if (!mOpenAttempted) {
        mOpenAttempted = true;
        mHidl = LegacyFingerprint::open();
        if (mHidl == nullptr) LOG(ERROR) << "Cannot open local fingerprint module";
    }
    return mHidl != nullptr;
}

::android::sp<IBiometricsFingerprint> FingerprintHidlConn::get() const {
    std::lock_guard<std::mutex> lock(mLock);
    return mHidl;
}

void FingerprintHidlConn::setStateCallback(FpStateCallback cb) {
    std::lock_guard<std::mutex> lock(mLock);
    mCb = std::move(cb);
}

}
