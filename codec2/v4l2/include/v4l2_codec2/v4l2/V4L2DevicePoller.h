// Copyright 2019 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_DEVICE_POLLER_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_DEVICE_POLLER_H

#include <atomic>

#include <base/callback_forward.h>
#include <base/sequence_checker.h>
#include <base/sequenced_task_runner.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>

namespace android {

class V4L2Device;

class V4L2DevicePoller {
public:

    using EventCallback = ::base::RepeatingCallback<void(bool event)>;

    V4L2DevicePoller(V4L2Device* const device, const std::string& threadName,
                     scoped_refptr<::base::SequencedTaskRunner> taskRunner);
    ~V4L2DevicePoller();

    bool startPolling(EventCallback eventCallback, ::base::RepeatingClosure errorCallback);

    bool stopPolling();

    bool isPolling() const;

    void schedulePoll();

private:

    void devicePollTask();

    V4L2Device* const mDevice;

    ::base::Thread mPollThread;

    EventCallback mEventCallback;

    ::base::RepeatingClosure mErrorCallback;

    scoped_refptr<::base::SequencedTaskRunner> mClientTaskTunner;

    std::atomic_bool mStopPolling;

    std::atomic_bool mPollBuffers;
};

}

#endif
