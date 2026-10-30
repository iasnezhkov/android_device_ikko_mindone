/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "StreamMindone.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <thread>
#include <utility>

#include <media/AidlConversionCppNdk.h>
#include <media/AidlConversionNdk.h>

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core::mindone {

DriverMindone::DriverMindone(StreamContext& context, bool isInput,
                             std::shared_ptr<LegacyBackend> backend)
    : mBackend(std::move(backend)), mDriverContext(context), mIsInput(isInput) {}

::android::status_t DriverMindone::init(DriverCallbackInterface*) {
    std::lock_guard lock(mMutex);
    if (hasStream()) return ::android::INVALID_OPERATION;
    if (mBackend == nullptr || mBackend->device() == nullptr) return ::android::NO_INIT;
    auto format = ::aidl::android::aidl2legacy_AudioFormatDescription_audio_format_t(
            mDriverContext.getFormat());
    auto channels = ::aidl::android::aidl2legacy_AudioChannelLayout_audio_channel_mask_t(
            mDriverContext.getChannelLayout(), mIsInput);
    auto flags = ::aidl::android::aidl2legacy_AudioIoFlags_audio_io_flags(
            mDriverContext.getFlags(), mIsInput);
    if (!format.ok() || !channels.ok() || !flags.ok() ||
        mDriverContext.getSampleRate() <= 0 || mDriverContext.getFrameSize() == 0) {
        return ::android::BAD_VALUE;
    }
    audio_config_t config = {};
    config.sample_rate = mDriverContext.getSampleRate();
    config.channel_mask = channels.value();
    config.format = format.value();
    const audio_config_t requested = config;
    auto* device = mBackend->device();
    int status;
    if (mIsInput) {
        status = mindone_audio_input_open(device, mDriverContext.getMixPortHandle(),
                                         AUDIO_DEVICE_NONE, flags.value().input, &config, &mInput);
    } else {
        status = mindone_audio_output_open(device, mDriverContext.getMixPortHandle(),
                                          AUDIO_DEVICE_NONE, flags.value().output, &config, &mOutput);
    }
    if (status != 0) {
        closeStream();
        return status;
    }
    if (config.sample_rate != requested.sample_rate ||
        config.channel_mask != requested.channel_mask || config.format != requested.format ||
        config.frame_count != requested.frame_count) {
        closeStream();
        return ::android::BAD_VALUE;
    }
    return hasStream() ? ::android::OK : ::android::NO_INIT;
}

::android::status_t DriverMindone::drain(StreamDescriptor::DrainMode) {
    std::lock_guard lock(mMutex);
    if (mIsInput) return ::android::OK;
    return mindone_audio_output_command(mOutput, MINDONE_AUDIO_DRAIN);
}

::android::status_t DriverMindone::flush() {
    if (mIsInput) return standby();
    std::lock_guard lock(mMutex);
    const int status = mindone_audio_output_command(mOutput, MINDONE_AUDIO_FLUSH);
    if (status == 0) {
        mStarted = true;
        mPaused = false;
    }
    return status;
}

::android::status_t DriverMindone::pause() {
    std::lock_guard lock(mMutex);
    if (!mIsInput) {
        const int status = mindone_audio_output_command(mOutput, MINDONE_AUDIO_PAUSE);
        if (status != 0) return status;
    }
    mPaused = true;
    return ::android::OK;
}

::android::status_t DriverMindone::standby() {
    std::lock_guard lock(mMutex);
    if (!hasStream()) return ::android::NO_INIT;
    const int status = mIsInput
            ? mindone_audio_input_command(mInput, MINDONE_AUDIO_STANDBY)
            : mindone_audio_output_command(mOutput, MINDONE_AUDIO_STANDBY);
    if (status == 0) {
        mStarted = false;
        mPaused = false;
    }
    return status;
}

::android::status_t DriverMindone::start() {
    std::lock_guard lock(mMutex);
    const int status = mIsInput
            ? mindone_audio_input_command(mInput, MINDONE_AUDIO_START)
            : mindone_audio_output_command(mOutput, MINDONE_AUDIO_START);
    if (status != 0) return status;
    mStarted = true;
    mPaused = false;
    return ::android::OK;
}

::android::status_t DriverMindone::transfer(void* buffer, size_t frameCount,
                                          size_t* actualFrameCount, int32_t* latencyMs) {
    std::lock_guard lock(mMutex);
    if (actualFrameCount == nullptr || latencyMs == nullptr) return ::android::BAD_VALUE;
    *actualFrameCount = 0;
    *latencyMs = 0;
    const size_t frameSize = mDriverContext.getFrameSize();
    if (frameSize == 0 || frameCount > std::numeric_limits<size_t>::max() / frameSize) {
        return ::android::BAD_VALUE;
    }
    const size_t bytes = frameCount * frameSize;
    if (bytes > static_cast<size_t>(std::numeric_limits<ssize_t>::max()) ||
        (buffer == nullptr && bytes != 0)) return ::android::BAD_VALUE;
    if (!hasStream()) return ::android::NO_INIT;
    if (!mIsInput && (!mStarted || mPaused)) return ::android::OK;
    if (!mIsInput && frameCount > std::numeric_limits<uint64_t>::max() - mFramesWritten) {
        return ::android::BAD_VALUE;
    }
    ssize_t actualBytes;
    if (mIsInput) {
        actualBytes = mindone_audio_input_read(mInput, buffer, bytes);
        size_t bufferBytes = 0;
        const int status = mindone_audio_input_buffer_size(mInput, &bufferBytes);
        if (status != 0) return status;
        const uint64_t bufferFrames = bufferBytes / frameSize;
        const uint64_t latency = bufferFrames * 1000 / mDriverContext.getSampleRate();
        *latencyMs = static_cast<int32_t>(
                std::min<uint64_t>(latency, std::numeric_limits<int32_t>::max()));
    } else {
        actualBytes = mindone_audio_output_write(mOutput, buffer, bytes);
        uint32_t latency = 0;
        const int status = mindone_audio_output_latency(mOutput, &latency);
        if (status != 0) return status;
        *latencyMs = static_cast<int32_t>(std::min<uint32_t>(
                latency, std::numeric_limits<int32_t>::max()));
    }
    if (actualBytes < 0) return static_cast<::android::status_t>(actualBytes);
    if (static_cast<size_t>(actualBytes) > bytes || actualBytes % frameSize != 0) return -EIO;
    *actualFrameCount = static_cast<size_t>(actualBytes) / frameSize;
    if (!mIsInput) mFramesWritten += *actualFrameCount;
    return ::android::OK;
}

::android::status_t DriverMindone::refinePosition(StreamDescriptor::Position* position) {
    std::lock_guard lock(mMutex);
    if (position == nullptr) return ::android::BAD_VALUE;
    if (mIsInput) {
        if (mInput == nullptr) return ::android::NO_INIT;
        int64_t frames = 0;
        int64_t timeNs = 0;
        const int status = mindone_audio_input_position(mInput, &frames, &timeNs);
        if (status != 0) return status;
        if (frames < 0 || timeNs <= 0) return -EIO;
        position->frames = frames;
        position->timeNs = timeNs;
    } else {
        if (mOutput == nullptr) return ::android::NO_INIT;
        uint64_t frames = 0;
        timespec timestamp = {};
        const int status = mindone_audio_output_position(mOutput, &frames, &timestamp);
        if (status != 0) return status;
        if (frames > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return -EIO;
        position->frames = static_cast<int64_t>(frames);
        position->timeNs = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec;
    }
    return ::android::OK;
}

bool DriverMindone::hasStream() const {
    return mInput != nullptr || mOutput != nullptr;
}

void DriverMindone::closeStream() {
    if (mInput != nullptr) {
        mindone_audio_input_close(mInput);
        mInput = nullptr;
    }
    if (mOutput != nullptr) {
        mindone_audio_output_close(mOutput);
        mOutput = nullptr;
    }
    mFramesWritten = 0;
    mStarted = false;
    mPaused = false;
}

void DriverMindone::shutdown() {
    std::lock_guard lock(mMutex);
    closeStream();
}

ndk::ScopedAStatus DriverMindone::routeDevices(const std::vector<AudioDevice>& devices) {
    if (devices.empty()) return ndk::ScopedAStatus::ok();
    if (devices.size() != 1) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    auto device = ::aidl::android::aidl2legacy_AudioDeviceDescription_audio_devices_t(
            devices.front().type);
    if (!device.ok() || audio_is_input_device(device.value()) != mIsInput) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard lock(mMutex);
    if (!hasStream()) return ndk::ScopedAStatus::fromServiceSpecificError(-ENODEV);
    const mindone_audio_route route = {device.value()};
    const int status = mIsInput ? mindone_audio_input_route(mInput, route)
                               : mindone_audio_output_route(mOutput, route);
    return status == 0 ? ndk::ScopedAStatus::ok()
                       : ndk::ScopedAStatus::fromServiceSpecificError(status);
}

StreamInMindone::StreamInMindone(StreamContext&& context, const SinkMetadata& sinkMetadata,
                                 const std::vector<MicrophoneInfo>& microphones,
                                 std::shared_ptr<LegacyBackend> backend)
    : StreamIn(std::move(context), microphones),
      StreamCommonImpl(&mContextInstance, sinkMetadata),
      DriverMindone(mContextInstance, true, std::move(backend)) {}

StreamInMindone::~StreamInMindone() {
    cleanupWorker();
}

ndk::ScopedAStatus StreamInMindone::setConnectedDevices(const std::vector<AudioDevice>& devices) {
    auto status = routeDevices(devices);
    return status.isOk() ? StreamCommonImpl::setConnectedDevices(devices) : std::move(status);
}

StreamOutMindone::StreamOutMindone(StreamContext&& context, const SourceMetadata& sourceMetadata,
                                   const std::optional<AudioOffloadInfo>& offloadInfo,
                                   std::shared_ptr<LegacyBackend> backend)
    : StreamOut(std::move(context), offloadInfo),
      StreamCommonImpl(&mContextInstance, sourceMetadata),
      DriverMindone(mContextInstance, false, std::move(backend)) {}

StreamOutMindone::~StreamOutMindone() {
    cleanupWorker();
}

ndk::ScopedAStatus StreamOutMindone::setConnectedDevices(const std::vector<AudioDevice>& devices) {
    auto status = routeDevices(devices);
    return status.isOk() ? StreamCommonImpl::setConnectedDevices(devices) : std::move(status);
}

}
