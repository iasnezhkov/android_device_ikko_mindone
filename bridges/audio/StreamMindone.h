/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <mutex>

#include "Backend.h"
#include "core-impl/Stream.h"

namespace aidl::android::hardware::audio::core::mindone {

class DriverMindone : virtual public DriverInterface {
  public:
    DriverMindone(StreamContext& context, bool isInput, std::shared_ptr<LegacyBackend> backend);

    ::android::status_t init(DriverCallbackInterface* callback) override;
    ::android::status_t drain(StreamDescriptor::DrainMode mode) override;
    ::android::status_t flush() override;
    ::android::status_t pause() override;
    ::android::status_t standby() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    ::android::status_t refinePosition(StreamDescriptor::Position* position) override;
    void shutdown() override;

  protected:
    ndk::ScopedAStatus routeDevices(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices);

  private:
    bool hasStream() const;
    void closeStream();

    std::shared_ptr<LegacyBackend> mBackend;
    StreamContext& mDriverContext;
    const bool mIsInput;
    mindone_stream_out* mOutput = nullptr;
    mindone_stream_in* mInput = nullptr;
    uint64_t mFramesWritten = 0;
    bool mStarted = false;
    bool mPaused = false;
    std::mutex mMutex;
};

class StreamInMindone final : public StreamIn, public StreamCommonImpl, public DriverMindone {
  public:
    friend class ndk::SharedRefBase;
    StreamInMindone(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones,
            std::shared_ptr<LegacyBackend> backend);
    ~StreamInMindone();

    ndk::ScopedAStatus setConnectedDevices(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices)
            override;

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }
};

class StreamOutMindone final : public StreamOut, public StreamCommonImpl, public DriverMindone {
  public:
    friend class ndk::SharedRefBase;
    StreamOutMindone(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>& offloadInfo,
            std::shared_ptr<LegacyBackend> backend);
    ~StreamOutMindone();

    ndk::ScopedAStatus setConnectedDevices(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices)
            override;

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }
};

}
