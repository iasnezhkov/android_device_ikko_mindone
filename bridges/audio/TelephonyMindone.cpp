/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "TelephonyMindone.h"

#include <cmath>
#include <utility>

using aidl::android::media::audio::common::AudioMode;
using aidl::android::media::audio::common::Boolean;
using aidl::android::media::audio::common::Float;

namespace aidl::android::hardware::audio::core::mindone {

ndk::ScopedAStatus setAudioMode(LegacyBackend& backend, AudioMode mode) {
    audio_mode_t legacyMode;
    switch (mode) {
        case AudioMode::NORMAL:
            legacyMode = AUDIO_MODE_NORMAL;
            break;
        case AudioMode::RINGTONE:
            legacyMode = AUDIO_MODE_RINGTONE;
            break;
        case AudioMode::IN_CALL:
            legacyMode = AUDIO_MODE_IN_CALL;
            break;
        case AudioMode::IN_COMMUNICATION:
            legacyMode = AUDIO_MODE_IN_COMMUNICATION;
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    return backend.setMode(legacyMode) == 0
                   ? ndk::ScopedAStatus::ok()
                   : ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
}

TelephonyMindone::TelephonyMindone(std::shared_ptr<LegacyBackend> backend)
    : mBackend(std::move(backend)) {
    mTelecomConfig.voiceVolume = Float{TelecomConfig::VOICE_VOLUME_MAX};
    mTelecomConfig.ttyMode = TelecomConfig::TtyMode::OFF;
    mTelecomConfig.isHacEnabled = Boolean{false};
}

ndk::ScopedAStatus TelephonyMindone::getSupportedAudioModes(std::vector<AudioMode>* modes) {
    *modes = {AudioMode::NORMAL, AudioMode::RINGTONE, AudioMode::IN_CALL,
              AudioMode::IN_COMMUNICATION};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TelephonyMindone::switchAudioMode(AudioMode mode) {
    return setAudioMode(*mBackend, mode);
}

ndk::ScopedAStatus TelephonyMindone::setTelecomConfig(const TelecomConfig& config,
                                                    TelecomConfig* result) {
    std::lock_guard lock(mMutex);
    if (config.voiceVolume.has_value()) {
        const float volume = config.voiceVolume.value().value;
        if (!std::isfinite(volume) || volume < TelecomConfig::VOICE_VOLUME_MIN ||
            volume > TelecomConfig::VOICE_VOLUME_MAX) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }
    if ((config.ttyMode != TelecomConfig::TtyMode::UNSPECIFIED &&
         config.ttyMode != mTelecomConfig.ttyMode) ||
        (config.isHacEnabled.has_value() &&
         config.isHacEnabled.value().value != mTelecomConfig.isHacEnabled.value().value)) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    if (config.voiceVolume.has_value()) {
        if (mBackend->setVoiceVolume(config.voiceVolume.value().value) != 0) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        mTelecomConfig.voiceVolume = config.voiceVolume;
    }
    *result = mTelecomConfig;
    return ndk::ScopedAStatus::ok();
}

}
