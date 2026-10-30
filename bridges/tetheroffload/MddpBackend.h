/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <aidl/android/hardware/tetheroffload/BnOffload.h>
#include <aidl/android/hardware/tetheroffload/ITetheringOffloadCallback.h>
#include <android-base/unique_fd.h>
#include "FlowParser.h"
#include <deque>
#include <map>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
namespace mindone::bridges::tetheroffload {
namespace aidl_to = ::aidl::android::hardware::tetheroffload;
class MddpBackend : public aidl_to::BnOffload {
  public:
    MddpBackend();
    ~MddpBackend() override;
    bool connect() const;
    ndk::ScopedAStatus initOffload(const ndk::ScopedFileDescriptor&, const ndk::ScopedFileDescriptor&,
        const std::shared_ptr<aidl_to::ITetheringOffloadCallback>&) override;
    ndk::ScopedAStatus stopOffload() override;
    ndk::ScopedAStatus setLocalPrefixes(const std::vector<std::string>&) override;
    ndk::ScopedAStatus getForwardedStats(const std::string&, aidl_to::ForwardedStats*) override;
    ndk::ScopedAStatus setDataWarningAndLimit(const std::string&, int64_t, int64_t) override;
    ndk::ScopedAStatus setUpstreamParameters(const std::string&, const std::string&, const std::string&,
        const std::vector<std::string>&) override;
    ndk::ScopedAStatus addDownstream(const std::string&, const std::string&) override;
    ndk::ScopedAStatus removeDownstream(const std::string&, const std::string&) override;
  private:
    void wake();
    void poison();
    void notice(int, std::vector<uint8_t> = {});
    bool request(uint32_t, uint32_t, void*, uint32_t);
    bool control(uint32_t, uint16_t);
    bool receive();
    void notification(uint32_t, uint32_t, uint16_t, const uint8_t*, size_t);
    bool deactivate();
    bool reconcile();
    bool drain();
    bool flushFlows();
    bool conntrack(int);
    bool admit(const Flow&, std::string*) const;
    bool inject(const Flow&, bool, const std::string&);
    void events();
    struct Notice {
        uint64_t generation;
        int event;
        std::vector<uint8_t> data;
    };
    std::deque<Notice> mNotices;
    std::map<std::string, aidl_to::ForwardedStats> mStats;
    std::map<std::string, Flow> mFlows;
    bool mV4 = false, mV6 = false, mQuota = false, mWarned = false;
    std::string mV4Address;
    std::mutex mLock, mDelivery;
    ::android::base::unique_fd mDevice, mWake, mConntrack1, mConntrack2;
    std::thread mEvents;
    std::atomic<bool> mQuit{false}, mReaderFailed{false};
    bool mInitialized = false, mActive = false;
    uint64_t mGeneration = 0;
    std::string mUpstream, mDownstream;
    std::set<std::pair<std::string, std::string>> mDownstreams;
    std::vector<std::string> mPrefixes;
    std::shared_ptr<aidl_to::ITetheringOffloadCallback> mCallback;
};
}
