/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// OffloadBridge - AIDL IOffload (V1) -> HIDL IOffloadConfig@1.0 + IOffloadControl@1.1 (stock tetheroffloadservice).
//
// Review 14.09 (BRIDGES-REVIEW-1409):
//  - binds through OffloadHidlConn instead of raw sp<>s, adding linkToDeath()+reconnect (the
//    previous version bound once at process start and never handled the HAL dying);
//  - tracks mInitialized and enforces the initOffload()/stopOffload() state machine IOffload.aidl
//    documents (EX_ILLEGAL_STATE) for the six methods the spec says "may only be called after
//    initOffload and before stopOffload" - the previous version had no such check anywhere and
//    always reported a generic EX_SERVICE_SPECIFIC instead;
//  - takes mLock in every method (previously only initOffload/stopOffload did), which matters
//    once the connection's sp<>s can change out from under a live call after a reconnect.
#pragma once

#include <aidl/android/hardware/tetheroffload/BnOffload.h>
#include <aidl/android/hardware/tetheroffload/ITetheringOffloadCallback.h>
#include <android/hardware/tetheroffload/control/1.1/ITetheringOffloadCallback.h>

#include "OffloadHidlConn.h"

#include <memory>
#include <mutex>

namespace mindone::bridges::tetheroffload {

namespace aidl_to = ::aidl::android::hardware::tetheroffload;

class HidlOffloadCallback : public hctl1::ITetheringOffloadCallback {
  public:
    explicit HidlOffloadCallback(std::shared_ptr<aidl_to::ITetheringOffloadCallback> cb) : mCb(std::move(cb)) {}
    ::android::hardware::Return<void> onEvent(hctl0::OffloadCallbackEvent event) override;
    ::android::hardware::Return<void> updateTimeout(const hctl0::NatTimeoutUpdate& params) override;
    ::android::hardware::Return<void> onEvent_1_1(hctl1::OffloadCallbackEvent event) override;

  private:
    std::shared_ptr<aidl_to::ITetheringOffloadCallback> mCb;
};

class OffloadBridge : public aidl_to::BnOffload {
  public:
    explicit OffloadBridge(std::shared_ptr<OffloadHidlConn> conn);

    bool connect();  // false = the stock HIDL services were not found at startup

    ndk::ScopedAStatus initOffload(const ndk::ScopedFileDescriptor& fd1, const ndk::ScopedFileDescriptor& fd2,
                                   const std::shared_ptr<aidl_to::ITetheringOffloadCallback>& cb) override;
    ndk::ScopedAStatus stopOffload() override;
    ndk::ScopedAStatus setLocalPrefixes(const std::vector<std::string>& prefixes) override;
    ndk::ScopedAStatus getForwardedStats(const std::string& upstream, aidl_to::ForwardedStats* out) override;
    ndk::ScopedAStatus setDataWarningAndLimit(const std::string& upstream, int64_t warningBytes,
                                              int64_t limitBytes) override;
    ndk::ScopedAStatus setUpstreamParameters(const std::string& iface, const std::string& v4Addr,
                                             const std::string& v4Gw,
                                             const std::vector<std::string>& v6Gws) override;
    ndk::ScopedAStatus addDownstream(const std::string& iface, const std::string& prefix) override;
    ndk::ScopedAStatus removeDownstream(const std::string& iface, const std::string& prefix) override;

  private:
    void onHidlStateChanged(bool available);
    // Returns a failure ScopedAStatus (EX_ILLEGAL_STATE) iff mInitialized != expected. Caller
    // must hold mLock. IOffload.aidl documents this exact state machine per-method (see the
    // "may only be called after initOffload and before stopOffload" language on six of the eight
    // methods; getForwardedStats is deliberately NOT gated - its own @throws list has no
    // EX_ILLEGAL_STATE entry).
    ndk::ScopedAStatus checkInitializedLocked(bool expected, const char* what);

    std::shared_ptr<OffloadHidlConn> mConn;
    std::mutex mLock;
    bool mInitialized = false;                                    // guarded by mLock
    std::shared_ptr<aidl_to::ITetheringOffloadCallback> mAidlCb;   // guarded by mLock - the client's own callback, set in initOffload
    ::android::sp<HidlOffloadCallback> mHidlCb;                    // guarded by mLock - our HIDL-side wrapper around mAidlCb
};

}  // namespace mindone::bridges::tetheroffload
