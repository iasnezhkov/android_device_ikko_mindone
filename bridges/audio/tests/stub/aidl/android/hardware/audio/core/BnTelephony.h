/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <aidl/android/hardware/audio/core/BnBluetooth.h>

namespace aidl::android::media::audio::common {

enum class AudioMode : int32_t {
    NORMAL = 0,
    RINGTONE = 1,
    IN_CALL = 2,
    IN_COMMUNICATION = 3,
};

}

namespace aidl::android::hardware::audio::core {

class ITelephony {
  public:
    struct TelecomConfig {
        static constexpr float VOICE_VOLUME_MIN = 0.0f;
        static constexpr float VOICE_VOLUME_MAX = 1.0f;

        enum class TtyMode : int32_t {
            UNSPECIFIED = 0,
            OFF = 1,
        };

        std::optional<::aidl::android::media::audio::common::Float> voiceVolume;
        TtyMode ttyMode = TtyMode::UNSPECIFIED;
        std::optional<::aidl::android::media::audio::common::Boolean> isHacEnabled;
    };

    virtual ~ITelephony() = default;
    virtual ndk::ScopedAStatus getSupportedAudioModes(
            std::vector<::aidl::android::media::audio::common::AudioMode>* modes) = 0;
    virtual ndk::ScopedAStatus switchAudioMode(
            ::aidl::android::media::audio::common::AudioMode mode) = 0;
    virtual ndk::ScopedAStatus setTelecomConfig(const TelecomConfig& config,
                                                TelecomConfig* result) = 0;
};

class BnTelephony : public ITelephony, public ndk::SharedRefBase {};

}
