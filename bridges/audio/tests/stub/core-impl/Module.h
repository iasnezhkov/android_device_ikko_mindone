/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <aidl/android/hardware/audio/core/BnBluetooth.h>
#include <aidl/android/hardware/audio/core/BnTelephony.h>

namespace aidl::android::hardware::audio::common {

struct SinkMetadata {};
struct SourceMetadata {};

}

namespace aidl::android::media::audio::common {

struct AudioOffloadInfo {};
struct MicrophoneInfo {};
struct AudioDeviceDescription {};
struct AudioDevice {
    AudioDeviceDescription type;
};

struct AudioPortExt {
    enum Tag {
        unspecified = 0,
        device = 1,
        mix = 2,
    };

    Tag getTag() const { return tag; }
    Tag tag = unspecified;
};

struct AudioPortConfig {
    int32_t id = 0;
    AudioPortExt ext;
};

}

namespace aidl::android::hardware::audio::core {

class StreamContext {};
class StreamIn {};
class StreamOut {};

struct AudioPatch {
    int32_t id = 0;
    std::vector<int32_t> sourcePortConfigIds;
    std::vector<int32_t> sinkPortConfigIds;
};

template <typename T>
class ChildInterface {
  public:
    ChildInterface& operator=(std::shared_ptr<T> instance) {
        mInstance = std::move(instance);
        return *this;
    }
    explicit operator bool() const { return mInstance != nullptr; }
    std::shared_ptr<T> getInstance() const { return mInstance; }

  private:
    std::shared_ptr<T> mInstance;
};

class Module : public ndk::SharedRefBase {
  public:
    enum class Type {
        DEFAULT,
    };

    struct Configuration {
        std::vector<AudioPatch> patches;
        std::vector<::aidl::android::media::audio::common::AudioPortConfig> portConfigs;
    };

    Module(Type, std::unique_ptr<Configuration>&& config)
        : mConfig(config ? std::move(config) : std::make_unique<Configuration>()) {}
    virtual ~Module() = default;

    virtual ndk::ScopedAStatus getTelephony(std::shared_ptr<ITelephony>* telephony) {
        if (telephony != nullptr) *telephony = nullptr;
        return ndk::ScopedAStatus::ok();
    }
    virtual ndk::ScopedAStatus getBluetooth(std::shared_ptr<IBluetooth>* bluetooth) {
        if (bluetooth != nullptr) *bluetooth = nullptr;
        return ndk::ScopedAStatus::ok();
    }
    virtual ndk::ScopedAStatus setAudioPatch(const AudioPatch& in_requested,
                                             AudioPatch* _aidl_return) {
        if (_aidl_return != nullptr) *_aidl_return = in_requested;
        return ndk::ScopedAStatus::ok();
    }
    virtual ndk::ScopedAStatus resetAudioPatch(int32_t) {
        return ndk::ScopedAStatus::ok();
    }
    virtual ndk::ScopedAStatus getMicMute(bool*) {
        return ndk::ScopedAStatus::ok();
    }
    virtual ndk::ScopedAStatus setMicMute(bool) {
        return ndk::ScopedAStatus::ok();
    }
    virtual ndk::ScopedAStatus getMasterMute(bool*) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    virtual ndk::ScopedAStatus setMasterMute(bool) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    virtual ndk::ScopedAStatus getMasterVolume(float*) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    virtual ndk::ScopedAStatus setMasterVolume(float) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

  protected:
    const Configuration& getConfig() const { return *mConfig; }

    template <typename StreamType, typename BaseType, typename... Args>
    ndk::ScopedAStatus createStreamInstance(std::shared_ptr<BaseType>* result, Args&&... args) {
        *result = std::make_shared<StreamType>(std::forward<Args>(args)...);
        return ndk::ScopedAStatus::ok();
    }

    virtual ndk::ScopedAStatus createInputStream(
            StreamContext&&,
            const ::aidl::android::hardware::audio::common::SinkMetadata&,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>&,
            std::shared_ptr<StreamIn>*) = 0;
    virtual ndk::ScopedAStatus createOutputStream(
            StreamContext&&,
            const ::aidl::android::hardware::audio::common::SourceMetadata&,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&,
            std::shared_ptr<StreamOut>*) = 0;
    virtual ndk::ScopedAStatus updateAudioMode(
            ::aidl::android::media::audio::common::AudioMode) = 0;

  private:
    std::unique_ptr<Configuration> mConfig;
};

}
