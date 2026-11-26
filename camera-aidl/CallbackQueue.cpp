/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CallbackQueue.h"

#include <pthread.h>

#include <thread>
#include <utility>

namespace mindone::camera {

std::shared_ptr<CallbackQueue> CallbackQueue::create() {
    std::shared_ptr<CallbackQueue> queue(new CallbackQueue());
    std::thread([queue] {
        pthread_setname_np(pthread_self(), "camprov-cb");
        queue->run();
    }).detach();
    return queue;
}

void CallbackQueue::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mJobs.push_back(std::move(job));
    }
    mReady.notify_one();
}

void CallbackQueue::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mReady.wait(lock, [this] { return !mJobs.empty(); });
            job = std::move(mJobs.front());
            mJobs.pop_front();
        }
        job();
    }
}

}
