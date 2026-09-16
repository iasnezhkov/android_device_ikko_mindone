/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "TetherOffloadBridge"

#include "OffloadBridge.h"

#include <android-base/logging.h>
#include <android/binder_status.h>
#include <cutils/native_handle.h>
#include <hidl/HidlSupport.h>
#include <unistd.h>

namespace mindone::bridges::tetheroffload {

using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

namespace {

// All the HIDL control methods reply with (bool success, string errMsg); AIDL expects ok/ServiceSpecificException.
struct BoolResult {
    bool ok = false;
    std::string err;
};

ndk::ScopedAStatus toAidl(const Return<void>& transport, const BoolResult& r, const char* what) {
    if (!transport.isOk()) {
        LOG(ERROR) << what << ": HIDL transport error: " << transport.description();
        return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(aidl_to::IOffload::ERROR_CODE_UNUSED,
                                                                       transport.description().c_str());
    }
    if (!r.ok) {
        LOG(WARNING) << what << ": " << r.err;
        return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(aidl_to::IOffload::ERROR_CODE_UNUSED,
                                                                       r.err.c_str());
    }
    return ndk::ScopedAStatus::ok();
}

// IOffload.aidl documents specific EX_ILLEGAL_STATE / EX_ILLEGAL_ARGUMENT exceptions per method
// (distinct from the single generic ERROR_CODE_UNUSED service-specific code used for genuine
// hardware/backend failures above) - the previous version of this bridge never threw either of
// these, reporting every failure (including simple state-machine misuse) as an opaque
// EX_SERVICE_SPECIFIC instead.
ndk::ScopedAStatus illegalState(const char* what) {
    LOG(ERROR) << what << ": called out of the initOffload()/stopOffload() sequence (EX_ILLEGAL_STATE)";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
}

ndk::ScopedAStatus illegalArgument(const char* what, const char* why) {
    LOG(ERROR) << what << ": " << why << " (EX_ILLEGAL_ARGUMENT)";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus notConnected(const char* what) {
    LOG(ERROR) << what << ": HIDL tetheroffload not connected right now";
    return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(aidl_to::IOffload::ERROR_CODE_UNUSED,
                                                                   "HIDL tetheroffload not connected");
}

auto collect(BoolResult& r) {
    return [&r](bool ok, const hidl_string& err) {
        r.ok = ok;
        r.err = err;
    };
}

hidl_vec<hidl_string> toHidl(const std::vector<std::string>& v) {
    hidl_vec<hidl_string> out(v.size());
    for (size_t i = 0; i < v.size(); i++) out[i] = v[i];
    return out;
}

// hidl_handle with ONE duplicated fd; owns it (closes it on destruction).
hidl_handle handleFromFd(const ndk::ScopedFileDescriptor& fd) {
    hidl_handle h;
    if (fd.get() < 0) return h;
    native_handle_t* nh = native_handle_create(1, 0);
    nh->data[0] = dup(fd.get());
    h.setTo(nh, true /*shouldOwn*/);
    return h;
}

}  // namespace

Return<void> HidlOffloadCallback::onEvent(hctl0::OffloadCallbackEvent event) {
    if (mCb) mCb->onEvent(static_cast<aidl_to::OffloadCallbackEvent>(static_cast<uint32_t>(event)));
    return Void();
}

Return<void> HidlOffloadCallback::onEvent_1_1(hctl1::OffloadCallbackEvent event) {
    if (mCb) mCb->onEvent(static_cast<aidl_to::OffloadCallbackEvent>(static_cast<uint32_t>(event)));
    return Void();
}

Return<void> HidlOffloadCallback::updateTimeout(const hctl0::NatTimeoutUpdate& p) {
    if (!mCb) return Void();
    aidl_to::NatTimeoutUpdate u;
    u.src.addr = p.src.addr;
    u.src.port = p.src.port;
    u.dst.addr = p.dst.addr;
    u.dst.port = p.dst.port;
    u.proto = static_cast<aidl_to::NetworkProtocol>(static_cast<uint32_t>(p.proto));
    mCb->updateTimeout(u);
    return Void();
}

OffloadBridge::OffloadBridge(std::shared_ptr<OffloadHidlConn> conn) : mConn(std::move(conn)) {
    mConn->setStateCallback([this](bool available) { onHidlStateChanged(available); });
}

bool OffloadBridge::connect() { return mConn->connect(); }

void OffloadBridge::onHidlStateChanged(bool available) {
    std::shared_ptr<aidl_to::ITetheringOffloadCallback> cb;
    {
        std::lock_guard<std::mutex> l(mLock);
        cb = mAidlCb;
        if (!available) {
            // The HIDL backend loses all its programmed state across a crash - the framework
            // must call initOffload() again before any control call will be accepted, exactly as
            // if it had called stopOffload() itself.
            mInitialized = false;
            mHidlCb = nullptr;
        }
    }
    if (!cb) return;  // no client has ever called initOffload() - nothing to notify
    if (!available) {
        // OFFLOAD_STOPPED_ERROR is IOffload.aidl's own documented vocabulary for exactly this:
        // "an error has occurred which has disrupted hardware acceleration ... statistics may be
        // temporarily unavailable". The previous version had no death handling at all, so a
        // backend crash mid-tether silently froze offload with no client-visible signal.
        LOG(ERROR) << "HIDL tetheroffload died - notifying OFFLOAD_STOPPED_ERROR";
        cb->onEvent(aidl_to::OffloadCallbackEvent::OFFLOAD_STOPPED_ERROR);
    } else {
        // OFFLOAD_SUPPORT_AVAILABLE: "the hardware management process is willing and able to
        // provide support ... If offload is desired, the client must reprogram it" - exactly the
        // reconnect outcome here (mInitialized is now false, so the client must call
        // initOffload() again to resume).
        LOG(INFO) << "HIDL tetheroffload reconnected - notifying OFFLOAD_SUPPORT_AVAILABLE";
        cb->onEvent(aidl_to::OffloadCallbackEvent::OFFLOAD_SUPPORT_AVAILABLE);
    }
}

ndk::ScopedAStatus OffloadBridge::checkInitializedLocked(bool expected, const char* what) {
    if (mInitialized != expected) return illegalState(what);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus OffloadBridge::initOffload(const ndk::ScopedFileDescriptor& fd1,
                                              const ndk::ScopedFileDescriptor& fd2,
                                              const std::shared_ptr<aidl_to::ITetheringOffloadCallback>& cb) {
    std::lock_guard<std::mutex> l(mLock);
    if (cb == nullptr) {
        return illegalArgument("initOffload", "null callback");
    }
    if (fd1.get() < 0 || fd2.get() < 0) {
        // "EX_ILLEGAL_ARGUMENT if any file descriptors are invalid" (IOffload.aidl) - the
        // previous version silently built an empty hidl_handle for an invalid fd and let the
        // HIDL backend discover the problem (if it even checks), reporting a generic
        // EX_SERVICE_SPECIFIC instead of the documented exception.
        return illegalArgument("initOffload", "invalid file descriptor");
    }
    // "If this API is called multiple times without first calling stopOffload, then the
    // subsequent calls must fail without changing the state of the server."
    if (auto st = checkInitializedLocked(false, "initOffload"); !st.isOk()) return st;

    auto config = mConn->config();
    auto control = mConn->control();
    if (config == nullptr || control == nullptr) return notConnected("initOffload");

    // Order matches OffloadHalHidlImpl.java: sockets first (config.setHandles), then control.initOffload.
    BoolResult r;
    auto t = config->setHandles(handleFromFd(fd1), handleFromFd(fd2), collect(r));
    auto st = toAidl(t, r, "setHandles");
    if (!st.isOk()) return st;
    auto hidlCb = ::android::sp<HidlOffloadCallback>(new HidlOffloadCallback(cb));
    r = BoolResult{};
    t = control->initOffload(hidlCb, collect(r));
    st = toAidl(t, r, "initOffload");
    if (!st.isOk()) return st;
    mHidlCb = hidlCb;
    mAidlCb = cb;
    mInitialized = true;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus OffloadBridge::stopOffload() {
    std::lock_guard<std::mutex> l(mLock);
    // "EX_ILLEGAL_STATE if initOffload() was not called, or if stopOffload() was already called."
    if (auto st = checkInitializedLocked(true, "stopOffload"); !st.isOk()) return st;
    auto control = mConn->control();
    if (control == nullptr) {
        // Nothing to tear down on a backend that is already gone - still clear our own state so
        // the framework can call initOffload() again once it comes back.
        mHidlCb = nullptr;
        mAidlCb = nullptr;
        mInitialized = false;
        return ndk::ScopedAStatus::ok();
    }
    BoolResult r;
    auto t = control->stopOffload(collect(r));
    auto st = toAidl(t, r, "stopOffload");
    mHidlCb = nullptr;
    mAidlCb = nullptr;
    mInitialized = false;
    return st;
}

ndk::ScopedAStatus OffloadBridge::setLocalPrefixes(const std::vector<std::string>& prefixes) {
    std::lock_guard<std::mutex> l(mLock);
    if (auto st = checkInitializedLocked(true, "setLocalPrefixes"); !st.isOk()) return st;
    auto control = mConn->control();
    if (control == nullptr) return notConnected("setLocalPrefixes");
    BoolResult r;
    auto t = control->setLocalPrefixes(toHidl(prefixes), collect(r));
    return toAidl(t, r, "setLocalPrefixes");
}

ndk::ScopedAStatus OffloadBridge::getForwardedStats(const std::string& upstream, aidl_to::ForwardedStats* out) {
    // IOffload.aidl's @throws list for getForwardedStats has no EX_ILLEGAL_STATE entry -
    // deliberately NOT gated on mInitialized, unlike the other six methods.
    auto control = mConn->control();
    if (control == nullptr) return notConnected("getForwardedStats");
    out->rxBytes = 0;
    out->txBytes = 0;
    auto t = control->getForwardedStats(upstream, [&](uint64_t rx, uint64_t tx) {
        out->rxBytes = static_cast<int64_t>(rx);
        out->txBytes = static_cast<int64_t>(tx);
    });
    return toAidl(t, BoolResult{true, ""}, "getForwardedStats");
}

ndk::ScopedAStatus OffloadBridge::setDataWarningAndLimit(const std::string& upstream, int64_t warningBytes,
                                                         int64_t limitBytes) {
    std::lock_guard<std::mutex> l(mLock);
    if (auto st = checkInitializedLocked(true, "setDataWarningAndLimit"); !st.isOk()) return st;
    auto control = mConn->control();
    auto control11 = mConn->control11();
    if (control == nullptr) return notConnected("setDataWarningAndLimit");
    BoolResult r;
    Return<void> t;
    if (control11 != nullptr) {
        t = control11->setDataWarningAndLimit(upstream, static_cast<uint64_t>(warningBytes),
                                              static_cast<uint64_t>(limitBytes), collect(r));
    } else {
        // 1.0-only backend has no warning concept at all (setDataLimit takes only a limit) - the
        // previous version silently dropped warningBytes here with no log whatsoever, so
        // OFFLOAD_WARNING_REACHED would just never fire and nobody could tell why.
        if (warningBytes != 0) {
            LOG(WARNING) << "setDataWarningAndLimit: only IOffloadControl@1.0 is bound - "
                            "warningBytes="
                        << warningBytes
                        << " has no 1.0 equivalent and will never fire OFFLOAD_WARNING_REACHED; "
                            "applying limitBytes only via setDataLimit";
        }
        t = control->setDataLimit(upstream, static_cast<uint64_t>(limitBytes), collect(r));
    }
    return toAidl(t, r, "setDataWarningAndLimit");
}

ndk::ScopedAStatus OffloadBridge::setUpstreamParameters(const std::string& iface, const std::string& v4Addr,
                                                        const std::string& v4Gw,
                                                        const std::vector<std::string>& v6Gws) {
    std::lock_guard<std::mutex> l(mLock);
    if (auto st = checkInitializedLocked(true, "setUpstreamParameters"); !st.isOk()) return st;
    auto control = mConn->control();
    if (control == nullptr) return notConnected("setUpstreamParameters");
    BoolResult r;
    auto t = control->setUpstreamParameters(iface, v4Addr, v4Gw, toHidl(v6Gws), collect(r));
    return toAidl(t, r, "setUpstreamParameters");
}

ndk::ScopedAStatus OffloadBridge::addDownstream(const std::string& iface, const std::string& prefix) {
    std::lock_guard<std::mutex> l(mLock);
    if (auto st = checkInitializedLocked(true, "addDownstream"); !st.isOk()) return st;
    auto control = mConn->control();
    if (control == nullptr) return notConnected("addDownstream");
    BoolResult r;
    auto t = control->addDownstream(iface, prefix, collect(r));
    return toAidl(t, r, "addDownstream");
}

ndk::ScopedAStatus OffloadBridge::removeDownstream(const std::string& iface, const std::string& prefix) {
    std::lock_guard<std::mutex> l(mLock);
    if (auto st = checkInitializedLocked(true, "removeDownstream"); !st.isOk()) return st;
    auto control = mConn->control();
    if (control == nullptr) return notConnected("removeDownstream");
    BoolResult r;
    auto t = control->removeDownstream(iface, prefix, collect(r));
    return toAidl(t, r, "removeDownstream");
}

}  // namespace mindone::bridges::tetheroffload
