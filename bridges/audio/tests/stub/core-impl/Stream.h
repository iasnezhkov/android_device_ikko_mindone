/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core-impl/Module.h"

namespace android {
using status_t = int32_t;
enum : status_t {
    OK = 0,
    BAD_VALUE = -22,
    INVALID_OPERATION = -38,
    NO_INIT = -19,
};
}

namespace aidl::android::hardware::audio::core {

struct StreamDescriptor {
    enum class DrainMode { ALL };
    enum class State { IDLE };
    struct Position {
        int64_t frames = 0;
        int64_t timeNs = 0;
    };
};

class DriverCallbackInterface {};

class DriverInterface {
  public:
    virtual ~DriverInterface() = default;
    virtual ::android::status_t init(DriverCallbackInterface*) = 0;
    virtual ::android::status_t drain(StreamDescriptor::DrainMode) = 0;
    virtual ::android::status_t flush() = 0;
    virtual ::android::status_t pause() = 0;
    virtual ::android::status_t standby() = 0;
    virtual ::android::status_t start() = 0;
    virtual ::android::status_t transfer(void*, size_t, size_t*, int32_t*) = 0;
    virtual ::android::status_t refinePosition(StreamDescriptor::Position*) = 0;
    virtual void shutdown() = 0;
};

class StreamCommonImpl {
  public:
    virtual ~StreamCommonImpl() = default;
    virtual ndk::ScopedAStatus setConnectedDevices(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>&) {
        return ndk::ScopedAStatus::ok();
    }
    virtual void onClose(StreamDescriptor::State) {}
    void defaultOnClose() {}
    void cleanupWorker() {}
};

}
