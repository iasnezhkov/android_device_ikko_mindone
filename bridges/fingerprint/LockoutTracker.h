/*
 * Copyright (C) 2022 The Android Open Source Project
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "LockoutStore.h"

#include <aidl/android/hardware/biometrics/fingerprint/ISessionCallback.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>

namespace mindone {

class LockoutTracker {
  public:
    using Callback = ::aidl::android::hardware::biometrics::fingerprint::ISessionCallback;

    explicit LockoutTracker(int32_t userId,
                            std::string directory = "/data/vendor/fingerprint_mindone")
        : store_(userId, std::move(directory)) {
        LockoutStore::State state;
        if (store_.load(state)) {
            failed_ = state.failed;
            failedTimed_ = state.failedTimed;
            if (state.deadlineMs != 0) {
                timed_ = true;
                deadlineMs_ = state.deadlineMs;
                deadline_ = Clock::now() + std::chrono::milliseconds(
                        std::clamp<int64_t>(deadlineMs_ - realtimeMs(), 0, 30000));
            }
        } else {
            failClosed();
            persist();
        }
        worker_ = std::thread([this] { run(); });
    }
    ~LockoutTracker() {
        {
            std::lock_guard<std::mutex> guard(lock_);
            stopping_ = true;
        }
        changed_.notify_all();
        worker_.join();
    }

    void attach(const std::shared_ptr<Callback>& cb) {
        std::lock_guard<std::mutex> guard(lock_);
        cb_ = cb;
        expire();
    }

    void detach(const std::shared_ptr<Callback>& cb) {
        std::lock_guard<std::mutex> guard(lock_);
        if (cb_.lock() == cb) cb_.reset();
    }

    bool notifyLockout(const std::shared_ptr<Callback>& cb) {
        std::lock_guard<std::mutex> guard(lock_);
        expire();
        if (failed_ >= 20) {
            cb->onLockoutPermanent();
            return true;
        }
        if (timed_) {
            auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                    deadline_ - Clock::now()).count();
            cb->onLockoutTimed(remaining > 0 ? remaining : 1);
            return true;
        }
        return false;
    }

    bool addFailedAttempt() {
        std::lock_guard<std::mutex> guard(lock_);
        expire();
        if (failed_ >= 20 || timed_) return true;
        ++failed_;
        if (++failedTimed_ >= 5 && failed_ < 20) {
            timed_ = true;
            deadline_ = Clock::now() + std::chrono::seconds(30);
            deadlineMs_ = realtimeMs() + 30000;
            changed_.notify_all();
        }
        if (failed_ >= 20) failedTimed_ = 0;
        if (!persist()) failClosed();
        return timed_ || failed_ >= 20;
    }

    void reset(const std::shared_ptr<Callback>& cb = nullptr) {
        std::lock_guard<std::mutex> guard(lock_);
        const int32_t previousFailed = failed_;
        failed_ = 0;
        failedTimed_ = 0;
        timed_ = false;
        clearPending_ = false;
        deadlineMs_ = 0;
        if (!persist()) {
            failed_ = previousFailed;
            failClosed();
            if (cb) {
                cb->onError(::aidl::android::hardware::biometrics::fingerprint::Error::UNABLE_TO_PROCESS,
                            0);
            }
            return;
        }
        changed_.notify_all();
        if (cb) cb->onLockoutCleared();
    }

  private:
    using Clock = std::chrono::steady_clock;

    static int64_t realtimeMs() {
        timespec now{};
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 0;
        return int64_t(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    }

    bool persist() {
        return store_.save({uint32_t(failed_), uint32_t(failedTimed_), deadlineMs_});
    }

    void failClosed() {
        if (failed_ >= 20) {
            failedTimed_ = 0;
            timed_ = false;
            deadlineMs_ = 0;
            clearPending_ = false;
            changed_.notify_all();
            return;
        }
        failed_ = std::max(failed_, 19);
        failedTimed_ = 5;
        timed_ = true;
        clearPending_ = false;
        deadlineMs_ = realtimeMs() + 30000;
        deadline_ = Clock::now() + std::chrono::seconds(30);
        changed_.notify_all();
    }

    void expire() {
        if (timed_ && Clock::now() >= deadline_) {
            timed_ = false;
            failedTimed_ = 0;
            deadlineMs_ = 0;
            if (!persist()) {
                failClosed();
                return;
            }
            clearPending_ = true;
        }
        if (clearPending_) {
            if (auto cb = cb_.lock()) {
                clearPending_ = false;
                cb->onLockoutCleared();
            }
        }
    }

    void run() {
        std::unique_lock<std::mutex> guard(lock_);
        while (!stopping_) {
            if (timed_) {
                auto deadline = deadline_;
                changed_.wait_until(guard, deadline);
                if (!stopping_) expire();
            } else {
                changed_.wait(guard, [this] { return stopping_ || timed_; });
            }
        }
    }

    LockoutStore store_;
    int64_t deadlineMs_ = 0;
    std::mutex lock_;
    std::condition_variable changed_;
    std::weak_ptr<Callback> cb_;
    int32_t failed_ = 0;
    int32_t failedTimed_ = 0;
    bool timed_ = false;
    bool clearPending_ = false;
    bool stopping_ = false;
    Clock::time_point deadline_{};
    std::thread worker_;
};

}
