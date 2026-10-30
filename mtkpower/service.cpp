/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <aidl/android/hardware/power/Boost.h>
#include <aidl/android/hardware/power/IPower.h>
#include <aidl/google/hardware/power/extension/pixel/IPowerExt.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>
#include <hwbinder/IPCThreadState.h>
#include <vendor/mediatek/hardware/mtkpower/1.2/IMtkPerf.h>
#include <vendor/mediatek/hardware/mtkpower/1.2/IMtkPower.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/system_properties.h>
#include <thread>
#include <unistd.h>

#include "Resources.h"

using aidl::android::hardware::power::IPower;
using aidl::android::hardware::power::Boost;
using aidl::android::hardware::power::Mode;
using aidl::google::hardware::power::extension::pixel::IPowerExt;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::Void;
namespace mtk = vendor::mediatek::hardware::mtkpower;
using Clock = std::chrono::steady_clock;
constexpr uint32_t kMaximumLifetimeMs = 300000;

static Return<void> unsupported(const char* method) {
    LOG(ERROR) << method << ": unsupported";
    return Void();
}

class Locks {
  public:
    Locks() : mBackendEvents(std::make_shared<BackendEvents>()), mDeath(new Death(*this)) {
        mNotifications = AServiceManager_registerForServiceNotifications(
                mBackendInstance.c_str(), backendRegistered, mBackendEvents.get());
        CHECK(mNotifications) << "Power backend notification registration failed";
        std::thread(watchProperty, mBackendEvents).detach();
        mThread = std::thread([this] { expire(); });
        mEventThread = std::thread([this] { dispatch(); });
    }
    ~Locks() {
        AServiceManager_NotificationRegistration_delete(mNotifications);
        {
            std::lock_guard guard(mMutex);
            mStop = true;
            changed();
            std::lock_guard backendGuard(mBackendEvents->mutex);
            mBackendEvents->stop = true;
        }
        mThread.join();
        mEventThread.join();
        mBackendDeath.set(nullptr);
    }

    int acquire(int handle, uint32_t duration, const hidl_vec<int32_t>& resources,
                std::set<std::string> hints = {}) {
        if (duration > INT32_MAX || (!resources.size() && hints.empty()) || resources.size() % 2) {
            return -EINVAL;
        }
        for (size_t i = 0; i < resources.size(); i += 2) {
            std::string hint = resourceHint(resources[i], resources[i + 1]);
            switch (resources[i]) {
                case 0x01408300:
                    if (resources[i + 1] == 50) hint = "MTK_UCLAMP_TA_50";
                    break;
                case 0x01438600:
                case 0x01438700:
                    LOG(WARNING) << "Ignoring obsolete MTK down-rate resource " << resources[i];
                    continue;
            }
            if (hint.empty()) {
                LOG(ERROR) << "Unsupported resource " << resources[i] << " value "
                           << resources[i + 1];
                return -EOPNOTSUPP;
            }
            hints.insert(hint);
        }
        if (hints.empty()) return -EOPNOTSUPP;
        if (!duration) {
            LOG(WARNING) << "Capping zero-duration lock at " << kMaximumLifetimeMs << " ms";
            duration = kMaximumLifetimeMs;
        }
        const auto* caller = android::hardware::IPCThreadState::self();
        std::lock_guard guard(mMutex);
        auto ext = mBackend;
        if (!ext) return -ENODEV;
        auto old = mLocks.find(handle);
        if (handle > 0) {
            if (old == mLocks.end()) return -ENOENT;
            if (old->second.uid != caller->getCallingUid() ||
                old->second.pid != caller->getCallingPid()) return -EPERM;
            if (old->second.until <= Clock::now()) return -ETIME;
        }
        for (const auto& hint : hints) {
            bool supported = false;
            if (!ext->isBoostSupported(hint, &supported).isOk()) return -EIO;
            if (!supported) return -EOPNOTSUPP;
        }
        if (mNext == std::numeric_limits<int32_t>::max()) return -EOVERFLOW;
        const int previous = handle > 0 ? handle : 0;
        handle = ++mNext;
        Lock request{caller->getCallingUid(), caller->getCallingPid(),
                     Clock::now() + std::chrono::milliseconds(duration), {}};
        mLocks.emplace(handle, request);
        for (const auto& hint : hints) {
            ++mReferences[hint];
            mLocks.at(handle).hints.insert(hint);
            if (!apply(ext, hint, request.until)) {
                mLocks.at(handle).until = Clock::now();
                releaseLocked(handle);
                changed();
                return -EIO;
            }
        }
        if (previous) {
            mLocks.at(previous).until = Clock::now();
            if (releaseLocked(previous)) {
                mLocks.at(handle).until = Clock::now();
                releaseLocked(handle);
                changed();
                return -EIO;
            }
            auto replacement = mLocks.extract(handle);
            replacement.key() = previous;
            mLocks.insert(std::move(replacement));
            handle = previous;
        }
        changed();
        return handle;
    }

    int release(int handle) {
        const auto* caller = android::hardware::IPCThreadState::self();
        std::lock_guard guard(mMutex);
        auto found = mLocks.find(handle);
        if (found == mLocks.end()) return -ENOENT;
        if (found->second.uid != caller->getCallingUid() ||
            (caller->getCallingPid() && found->second.pid != caller->getCallingPid())) {
            return -EPERM;
        }
        found->second.until = Clock::now();
        int result = releaseLocked(handle);
        changed();
        return result;
    }

    int watch(const android::sp<mtk::V1_1::IMtkPowerCallback>& callback) {
        if (!callback) return -EINVAL;
        auto* caller = android::hardware::IPCThreadState::self();
        uint64_t cookie;
        {
            std::lock_guard guard(mMutex);
            if (mCallbacks.size() >= 256) return -ENOSPC;
            cookie = ++mCookie;
            mCallbacks.emplace(cookie, Callback{caller->getCallingUid(), caller->getCallingPid(),
                                                callback});
        }
        auto result = callback->linkToDeath(mDeath, cookie);
        if (result.isOk() && static_cast<bool>(result)) return 0;
        std::lock_guard guard(mMutex);
        mCallbacks.erase(cookie);
        return -EIO;
    }

    void hintEvent(int hint, int duration) {
        std::lock_guard guard(mMutex);
        for (const auto& entry : mCallbacks) {
            if (mEvents.size() >= 1024) {
                LOG(ERROR) << "Callback queue full; dropping hint event";
                break;
            }
            auto callback = entry.second.binder;
            mEvents.emplace_back([callback, hint, duration] {
                if (!callback->mtkPowerHint(hint, duration).isOk()) LOG(ERROR) << "Hint callback failed";
            });
        }
        changed();
    }

    void appEvent(const hidl_string& pack, const hidl_string& act, int pid, int state, int uid) {
        std::lock_guard guard(mMutex);
        for (const auto& entry : mCallbacks) {
            if (mEvents.size() >= 1024) {
                LOG(ERROR) << "Callback queue full; dropping app event";
                break;
            }
            auto callback = entry.second.binder;
            mEvents.emplace_back([callback, pack, act, pid, state, uid] {
                if (!callback->notifyAppState(pack, act, pid, state, uid).isOk()) {
                    LOG(ERROR) << "App-state callback failed";
                }
            });
        }
        changed();
    }

    bool interaction(int duration) {
        std::lock_guard guard(mMutex);
        bool supported = false;
        return mPower && mPower->isBoostSupported(Boost::INTERACTION, &supported).isOk() &&
               supported && mPower->setBoost(Boost::INTERACTION, duration).isOk();
    }

    int audio(int hint, int data) {
        auto* caller = android::hardware::IPCThreadState::self();
        std::lock_guard guard(mMutex);
        const auto owner = std::make_pair(caller->getCallingUid(), hint);
        if (data < 0) return -EINVAL;
        if (data) {
            LOG(WARNING) << "Capping audio owner " << owner.first << " hint " << hint
                         << " at " << kMaximumLifetimeMs << " ms";
            auto service = mPower;
            bool supported = false;
            if (!service || !service->isModeSupported(Mode::AUDIO_STREAMING_LOW_LATENCY,
                                                     &supported).isOk()) return -ENODEV;
            if (!supported) return -EOPNOTSUPP;
            if (!service->setMode(Mode::AUDIO_STREAMING_LOW_LATENCY, true).isOk()) return -EIO;
            mAudio[owner] = Clock::now() + std::chrono::milliseconds(kMaximumLifetimeMs);
        } else {
            if (!mAudio.erase(owner)) return -ENOENT;
        }
        mAudioDirty = true;
        changed();
        return syncAudio() ? 0 : -EIO;
    }

  private:
    struct BackendEvents {
        std::mutex mutex;
        std::condition_variable changed;
        ndk::SpAIBinder registered;
        std::set<uint64_t> deaths;
        bool ready = false;
        bool wake = false;
        bool stop = false;
    };
    struct BackendCookie {
        std::shared_ptr<BackendEvents> events;
        uint64_t generation;
    };

    static void backendRegistered(const char*, AIBinder* binder, void* cookie) {
        auto& events = *static_cast<BackendEvents*>(cookie);
        std::lock_guard guard(events.mutex);
        events.registered = ndk::SpAIBinder(binder);
        events.wake = true;
        events.changed.notify_all();
    }

    static void backendDied(void* cookie) {
        auto& death = *static_cast<BackendCookie*>(cookie);
        std::lock_guard guard(death.events->mutex);
        death.events->deaths.insert(death.generation);
        death.events->wake = true;
        death.events->changed.notify_all();
    }

    static void watchProperty(std::shared_ptr<BackendEvents> events) {
        const char* name = "vendor.powerhal.init";
        uint32_t serial = __system_property_area_serial();
        const prop_info* property;
        while (!(property = __system_property_find(name))) {
            if (!__system_property_wait(nullptr, serial, &serial, nullptr)) return;
            std::lock_guard guard(events->mutex);
            if (events->stop) return;
        }
        for (;;) {
            serial = __system_property_serial(property);
            const bool ready = android::base::GetBoolProperty(name, false);
            {
                std::lock_guard guard(events->mutex);
                if (events->stop) return;
                if (events->ready != ready) {
                    events->ready = ready;
                    events->wake = true;
                    events->changed.notify_all();
                }
            }
            if (!__system_property_wait(property, serial, &serial, nullptr)) return;
        }
    }

    void changed() {
        mChanged.notify_all();
        std::lock_guard guard(mBackendEvents->mutex);
        mBackendEvents->wake = true;
        mBackendEvents->changed.notify_all();
    }

    void dropBackend() {
        ++mGeneration;
        mBackendDeath.set(nullptr);
        mBackend.reset();
        mPower.reset();
        mAudioDirty |= !mAudio.empty();
        mReplay = true;
        mReplayAt = Clock::time_point::min();
        mReplayDelay = std::chrono::seconds(1);
    }

    void updateBackend() {
        ndk::SpAIBinder registered;
        bool dead;
        bool ready;
        {
            std::lock_guard guard(mBackendEvents->mutex);
            registered = std::move(mBackendEvents->registered);
            dead = mBackendEvents->deaths.count(mGeneration);
            mBackendEvents->deaths.clear();
            ready = mBackendEvents->ready;
            mBackendEvents->wake = false;
        }
        if (dead) {
            dropBackend();
            mBinder.set(nullptr);
        }
        if (mReady && !ready) dropBackend();
        mReady = ready;
        if (registered.get() && registered.get() != mBinder.get()) {
            dropBackend();
            mBinder = std::move(registered);
        }
    }

    bool connect() {
        if (!mBackendDeath.get()) {
            ndk::ScopedAIBinder_DeathRecipient death(AIBinder_DeathRecipient_new(backendDied));
            AIBinder_DeathRecipient_setOnUnlinked(death.get(), [](void* cookie) {
                delete static_cast<BackendCookie*>(cookie);
            });
            auto* cookie = new BackendCookie{mBackendEvents, ++mGeneration};
            const auto status = AIBinder_linkToDeath(mBinder.get(), death.get(), cookie);
            if (status != STATUS_OK) {
                if (status == STATUS_DEAD_OBJECT) mBinder.set(nullptr);
                return false;
            }
            mBackendDeath = std::move(death);
        }
        if (!mPower) mPower = IPower::fromBinder(mBinder);
        if (mPower && !mBackend) {
            ndk::SpAIBinder binder;
            if (AIBinder_getExtension(mBinder.get(), binder.getR()) == STATUS_OK) {
                mBackend = IPowerExt::fromBinder(binder);
            }
        }
        return mPower && mBackend;
    }

    struct Callback {
        uid_t uid;
        pid_t pid;
        android::sp<mtk::V1_1::IMtkPowerCallback> binder;
    };
    class Death : public android::hardware::hidl_death_recipient {
      public:
        explicit Death(Locks& locks) : mLocks(locks) {}
        void serviceDied(uint64_t cookie,
                         const android::wp<android::hidl::base::V1_0::IBase>&) override {
            std::lock_guard guard(mLocks.mMutex);
            auto owner = mLocks.mCallbacks.find(cookie);
            if (owner == mLocks.mCallbacks.end()) return;
            for (auto& entry : mLocks.mLocks) {
                if (entry.second.uid == owner->second.uid && entry.second.pid == owner->second.pid) {
                    entry.second.until = Clock::now();
                }
            }
            mLocks.mCallbacks.erase(owner);
            mLocks.changed();
        }
      private:
        Locks& mLocks;
    };
    struct Lock {
        uid_t uid;
        pid_t pid;
        Clock::time_point until;
        std::set<std::string> hints;
    };

    bool apply(const std::shared_ptr<IPowerExt>& ext, const std::string& hint,
               Clock::time_point until) {
        for (const auto& entry : mLocks) {
            if (entry.second.hints.count(hint)) until = std::max(until, entry.second.until);
        }
        auto duration = std::chrono::ceil<std::chrono::milliseconds>(until - Clock::now());
        auto milliseconds = std::clamp<int64_t>(duration.count(), 1, INT32_MAX);
        return ext->setBoost(hint, static_cast<int32_t>(milliseconds)).isOk();
    }

    int releaseLocked(int handle) {
        auto& hints = mLocks.at(handle).hints;
        auto ext = mBackend;
        for (auto it = hints.begin(); it != hints.end();) {
            auto reference = mReferences.find(*it);
            if (reference->second == 1) {
                if (!ext || !ext->setBoost(*it, -1).isOk()) {
                    ++it;
                    continue;
                }
                mReferences.erase(reference);
            } else {
                if (!ext || !apply(ext, *it, Clock::now())) {
                    ++it;
                    continue;
                }
                --reference->second;
            }
            it = hints.erase(it);
        }
        if (!hints.empty()) return -EIO;
        mLocks.erase(handle);
        return 0;
    }

    bool syncAudio() {
        if (!mAudioDirty) return true;
        auto service = mPower;
        bool supported = false;
        if (!service || !service->isModeSupported(Mode::AUDIO_STREAMING_LOW_LATENCY,
                                                 &supported).isOk() || !supported ||
            !service->setMode(Mode::AUDIO_STREAMING_LOW_LATENCY,
                                        !mAudio.empty()).isOk()) return false;
        mAudioDirty = false;
        return true;
    }

    void recover() {
        if (!mReady || !mBinder.get() || !mReplay || Clock::now() < mReplayAt) return;
        bool restored = connect();
        if (restored) {
            for (const auto& entry : mReferences) {
                bool active = false;
                for (const auto& request : mLocks) {
                    active |= request.second.until > Clock::now() &&
                              request.second.hints.count(entry.first);
                }
                if (active) {
                    bool supported = false;
                    if (!mBackend->isBoostSupported(entry.first, &supported).isOk() || !supported ||
                        !apply(mBackend, entry.first, Clock::now())) restored = false;
                }
            }
        }
        mAudioDirty |= !mAudio.empty();
        if (restored && syncAudio()) {
            mReplay = false;
            mReplayAt = Clock::time_point::max();
            mReplayDelay = std::chrono::seconds(1);
            LOG(INFO) << "Power backend connected; active hints restored";
        } else {
            mReplayAt = Clock::now() + mReplayDelay;
            mReplayDelay = std::min(mReplayDelay * 2, std::chrono::seconds(30));
            LOG(ERROR) << "Power backend replay failed; retrying";
        }
    }

    void expire() {
        std::unique_lock guard(mMutex);
        while (!mStop) {
            updateBackend();
            auto next = Clock::time_point::max();
            for (auto it = mAudio.begin(); it != mAudio.end();) {
                if (it->second <= Clock::now()) {
                    LOG(WARNING) << "Audio owner expired: " << it->first.first;
                    it = mAudio.erase(it);
                    mAudioDirty = true;
                } else {
                    next = std::min(next, it->second);
                    ++it;
                }
            }
            recover();
            if (!mReplay && !syncAudio()) {
                next = std::min(next, Clock::now() + std::chrono::seconds(1));
            }
            for (auto it = mLocks.begin(); it != mLocks.end();) {
                const int handle = it->first;
                const auto until = it->second.until;
                ++it;
                if (until <= Clock::now()) {
                    if (releaseLocked(handle)) {
                        LOG(ERROR) << "Release retry for " << handle;
                        next = std::min(next, Clock::now() + std::chrono::seconds(1));
                    }
                } else {
                    next = std::min(next, until);
                }
            }
            if (mReady && mBinder.get() && mReplay) next = std::min(next, mReplayAt);
            std::unique_lock backendGuard(mBackendEvents->mutex);
            guard.unlock();
            auto pending = [this] { return mBackendEvents->wake; };
            if (next == Clock::time_point::max()) {
                mBackendEvents->changed.wait(backendGuard, pending);
            } else {
                mBackendEvents->changed.wait_until(backendGuard, next, pending);
            }
            backendGuard.unlock();
            guard.lock();
        }
    }

    void dispatch() {
        std::unique_lock guard(mMutex);
        while (!mStop) {
            mChanged.wait(guard, [this] { return mStop || !mEvents.empty(); });
            auto events = std::move(mEvents);
            mEvents.clear();
            guard.unlock();
            for (auto& event : events) event();
            guard.lock();
        }
    }

    std::mutex mMutex;
    std::condition_variable mChanged;
    std::map<int, Lock> mLocks;
    std::map<std::string, size_t> mReferences;
    std::map<uint64_t, Callback> mCallbacks;
    std::map<std::pair<uid_t, int>, Clock::time_point> mAudio;
    std::shared_ptr<BackendEvents> mBackendEvents;
    const std::string mBackendInstance = std::string(IPower::descriptor) + "/default";
    AServiceManager_NotificationRegistration* mNotifications = nullptr;
    ndk::ScopedAIBinder_DeathRecipient mBackendDeath;
    ndk::SpAIBinder mBinder;
    std::shared_ptr<IPower> mPower;
    std::shared_ptr<IPowerExt> mBackend;
    uint64_t mGeneration = 0;
    bool mReady = false;
    bool mReplay = true;
    Clock::time_point mReplayAt = Clock::time_point::min();
    std::chrono::seconds mReplayDelay{1};
    std::deque<std::function<void()>> mEvents;
    bool mAudioDirty = false;
    uint64_t mCookie = 0;
    int32_t mNext = 0;
    bool mStop = false;
    android::sp<Death> mDeath;
    std::thread mThread;
    std::thread mEventThread;
};

class Perf : public mtk::V1_2::IMtkPerf {
  public:
    explicit Perf(Locks& locks) : mLocks(locks) {}
    Return<int32_t> perfLockAcquire(int32_t handle, uint32_t duration,
                                    const hidl_vec<int32_t>& resources, int32_t reserved) override {
        return reserved ? -EOPNOTSUPP : mLocks.acquire(handle, duration, resources);
    }
    Return<void> perfLockRelease(int32_t handle, int32_t reserved) override {
        int result = reserved ? -EOPNOTSUPP : mLocks.release(handle);
        if (result) LOG(ERROR) << "perfLockRelease: " << result;
        return Void();
    }
    Return<int32_t> perfLockReleaseSync(int32_t handle, int32_t reserved) override {
        return reserved ? -EOPNOTSUPP : mLocks.release(handle);
    }
    Return<int32_t> perfCusLockHint(int32_t hint, uint32_t duration) override {
        auto name = scenarioHint(hint);
        int result = name.empty() ? -EOPNOTSUPP : mLocks.acquire(0, duration, {}, {name});
        if (result > 0) mLocks.hintEvent(hint, duration ? duration : kMaximumLifetimeMs);
        return result;
    }

  private:
    Locks& mLocks;
};

class Power : public mtk::V1_2::IMtkPower {
  public:
    explicit Power(Locks& locks) : mLocks(locks) {}
    Return<void> mtkPowerHint(int32_t hint, int32_t data) override {
        if (hint == 25 && data > 0) {
            if (!mLocks.interaction(data)) {
                LOG(ERROR) << "Interaction power boost failed";
            } else {
                mLocks.hintEvent(hint, data);
            }
            return Void();
        }
        if (!scenarioHint(hint).empty()) return mtkCusPowerHint(hint, data);
        return mtkCusPowerHint(hint, data);
    }
    Return<void> mtkCusPowerHint(int32_t hint, int32_t duration) override {
        if (hint == 36 || hint == 37 || hint == 38 || hint == 44 || hint == 46) {
            int result = mLocks.audio(hint, duration);
            if (result) LOG(ERROR) << "Audio power mode failed: " << result;
            else mLocks.hintEvent(hint, duration ? kMaximumLifetimeMs : 0);
            return Void();
        }
        auto name = scenarioHint(hint);
        if (name.empty() || duration <= 0) return unsupported("mtkCusPowerHint");
        int result = mLocks.acquire(0, duration, {}, {name});
        if (result < 0) LOG(ERROR) << "mtkCusPowerHint failed: " << result;
        else mLocks.hintEvent(hint, duration);
        return Void();
    }
    Return<void> notifyAppState(const hidl_string& pack, const hidl_string& act, int32_t pid,
                                int32_t state, int32_t uid) override {
        mLocks.appEvent(pack, act, pid, state, uid);
        return Void();
    }
    Return<int32_t> querySysInfo(int32_t, int32_t) override { return -EOPNOTSUPP; }
    Return<int32_t> setSysInfo(int32_t, const hidl_string&) override { return -EOPNOTSUPP; }
    Return<void> setSysInfoAsync(int32_t, const hidl_string&) override {
        return unsupported("setSysInfoAsync");
    }
    Return<int32_t> setMtkPowerCallback(
            const android::sp<mtk::V1_1::IMtkPowerCallback>& callback) override {
        return mLocks.watch(callback);
    }
    Return<int32_t> setMtkScnUpdateCallback(
            int32_t, const android::sp<mtk::V1_2::IMtkPowerCallback>&) override {
        return -EOPNOTSUPP;
    }
  private:
    Locks& mLocks;
};

int main() {
    android::base::SetDefaultTag("mtkpower-mindone");
    if (!ABinderProcess_setThreadPoolMaxThreadCount(1)) return EXIT_FAILURE;
    ABinderProcess_startThreadPool();
    android::hardware::configureRpcThreadpool(2, true);
    Locks locks;
    android::sp<Perf> perf = new Perf(locks);
    android::sp<Power> powerService = new Power(locks);
    if (perf->registerAsService() != android::OK ||
        powerService->registerAsService() != android::OK) return EXIT_FAILURE;
    android::hardware::joinRpcThreadpool();
    return EXIT_FAILURE;
}
