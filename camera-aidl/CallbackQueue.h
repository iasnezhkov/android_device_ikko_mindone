/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace mindone::camera {

class CallbackQueue {
  public:
    static std::shared_ptr<CallbackQueue> create();
    void post(std::function<void()> job);

  private:
    CallbackQueue() = default;
    void run();

    std::mutex mMutex;
    std::condition_variable mReady;
    std::deque<std::function<void()>> mJobs;
};

}
