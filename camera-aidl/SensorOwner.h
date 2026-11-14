/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace mindone::camera {

class SensorOwner : public std::enable_shared_from_this<SensorOwner> {
  public:
    using Clock = std::chrono::steady_clock;

    struct Timing {
        Clock::duration settle;
        Clock::duration window;
        Clock::duration retry;
        Clock::duration firstDelay;
        Clock::duration lastDelay;
    };

    static constexpr Timing kTiming{std::chrono::milliseconds(200), std::chrono::seconds(3),
                                    std::chrono::milliseconds(600), std::chrono::milliseconds(40),
                                    std::chrono::milliseconds(160)};

    class Hold {
      public:
        Hold(std::shared_ptr<SensorOwner> owner, std::string name, std::string previous,
             Clock::time_point settled)
            : mOwner(std::move(owner)), mName(std::move(name)), mPrevious(std::move(previous)),
              mSettled(settled) {}
        ~Hold();
        const std::string& name() const { return mName; }
        bool handover() const { return !mPrevious.empty(); }
        const std::string& previous() const { return mPrevious; }
        Clock::time_point settled() const { return mSettled; }
        const Timing& timing() const { return mOwner->mTiming; }

      private:
        const std::shared_ptr<SensorOwner> mOwner;
        const std::string mName;
        const std::string mPrevious;
        const Clock::time_point mSettled;
    };

    explicit SensorOwner(Timing timing = kTiming,
                         std::function<Clock::time_point()> now = Clock::now)
        : mTiming(timing), mNow(std::move(now)) {}

    std::unique_ptr<Hold> acquire(const std::string& name, bool* absent);
    bool absent(const std::string& name);
    void setAbsent(const std::set<std::string>& names);

  private:
    const Timing mTiming;
    const std::function<Clock::time_point()> mNow;
    std::mutex mMutex;
    std::string mName;
    std::set<std::string> mAbsent;
    int mHolds = 0;
    std::string mReleased;
    Clock::time_point mReleasedAt;
};

struct HandoverSteps {
    std::function<bool()> configure;
    std::function<void()> close;
    std::function<bool()> reopen;
    std::function<SensorOwner::Clock::time_point()> now;
    std::function<void(SensorOwner::Clock::duration)> sleep;
};

struct HandoverOutcome {
    bool open = true;
    int reopens = 0;
};

HandoverOutcome retryHandover(const HandoverSteps& steps, const SensorOwner::Timing& timing,
                              SensorOwner::Clock::time_point deadline);

}
