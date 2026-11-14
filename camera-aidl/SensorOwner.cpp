/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SensorOwner.h"

#include <algorithm>
#include <thread>

namespace mindone::camera {

SensorOwner::Hold::~Hold() {
    std::lock_guard<std::mutex> lock(mOwner->mMutex);
    if (--mOwner->mHolds == 0) {
        mOwner->mReleased = mOwner->mName;
        mOwner->mReleasedAt = mOwner->mNow();
    }
}

std::unique_ptr<SensorOwner::Hold> SensorOwner::acquire(const std::string& name, bool* absent) {
    std::lock_guard<std::mutex> lock(mMutex);
    *absent = mAbsent.count(name) != 0;
    if (*absent || (mHolds && mName != name)) return nullptr;
    std::string previous;
    Clock::time_point settled;
    if (!mHolds && !mReleased.empty() && mReleased != name &&
        mNow() - mReleasedAt < mTiming.window) {
        previous = mReleased;
        settled = mReleasedAt + mTiming.settle;
    }
    mName = name;
    ++mHolds;
    return std::make_unique<Hold>(shared_from_this(), name, std::move(previous), settled);
}

bool SensorOwner::absent(const std::string& name) {
    std::lock_guard<std::mutex> lock(mMutex);
    return mAbsent.count(name) != 0;
}

void SensorOwner::setAbsent(const std::set<std::string>& names) {
    std::lock_guard<std::mutex> lock(mMutex);
    mAbsent = names;
}

HandoverOutcome retryHandover(const HandoverSteps& steps, const SensorOwner::Timing& timing,
                              SensorOwner::Clock::time_point deadline) {
    auto now = steps.now ? steps.now : [] { return SensorOwner::Clock::now(); };
    auto sleep = steps.sleep ? steps.sleep
                             : [](SensorOwner::Clock::duration delay) {
                                   std::this_thread::sleep_for(delay);
                               };
    HandoverOutcome outcome;
    if (now() + timing.firstDelay > deadline) return outcome;
    steps.close();
    outcome.open = false;
    for (auto delay = timing.firstDelay; now() + delay <= deadline;
         delay = std::min(delay * 2, timing.lastDelay)) {
        sleep(delay);
        if (!steps.reopen()) continue;
        ++outcome.reopens;
        if (!steps.configure()) {
            outcome.open = true;
            return outcome;
        }
        steps.close();
    }
    return outcome;
}

}
