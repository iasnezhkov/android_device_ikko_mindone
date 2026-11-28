/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "BluetoothMindone.h"

#include <cmath>
#include <optional>
#include <utility>

using aidl::android::media::audio::common::Boolean;
using aidl::android::media::audio::common::Float;
using aidl::android::media::audio::common::Int;

namespace aidl::android::hardware::audio::core::mindone {

BluetoothMindone::BluetoothMindone(std::shared_ptr<LegacyBackend> backend)
    : mBackend(std::move(backend)) {
    mScoConfig.isEnabled = Boolean{false};
    mScoConfig.isNrecEnabled = Boolean{false};
    mScoConfig.mode = ScoConfig::Mode::SCO;
    mHfpConfig.isEnabled = Boolean{false};
    mHfpConfig.sampleRate = Int{8000};
    mHfpConfig.volume = Float{HfpConfig::VOLUME_MAX};
}

ndk::ScopedAStatus BluetoothMindone::setScoConfig(const ScoConfig& config, ScoConfig* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard lock(mMutex);
    std::optional<mindone_audio_sco_band> targetBand;
    switch (config.mode) {
        case ScoConfig::Mode::UNSPECIFIED:
            break;
        case ScoConfig::Mode::SCO:
            targetBand = MINDONE_AUDIO_SCO_NB;
            break;
        case ScoConfig::Mode::SCO_WB:
            targetBand = MINDONE_AUDIO_SCO_WB;
            break;
        case ScoConfig::Mode::SCO_SWB:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (targetBand.has_value()) {
        if (mBackend == nullptr || mBackend->setSco(*targetBand) != 0) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        mScoConfig.mode = config.mode;
    }
    if (config.isEnabled.has_value()) {
        mScoConfig.isEnabled = config.isEnabled;
    }
    if (config.isNrecEnabled.has_value()) {
        mScoConfig.isNrecEnabled = config.isNrecEnabled;
    }
    if (config.debugName.has_value()) {
        mScoConfig.debugName = config.debugName;
    }
    *result = mScoConfig;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothMindone::setHfpConfig(const HfpConfig& config, HfpConfig* result) {
    if (result == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard lock(mMutex);
    if (config.sampleRate.has_value() && config.sampleRate.value().value <= 0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (config.volume.has_value()) {
        const float volume = config.volume.value().value;
        if (!std::isfinite(volume) || volume < HfpConfig::VOLUME_MIN ||
            volume > HfpConfig::VOLUME_MAX) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }
    if (config.isEnabled.has_value()) {
        mHfpConfig.isEnabled = config.isEnabled;
    }
    if (config.sampleRate.has_value()) {
        mHfpConfig.sampleRate = config.sampleRate;
    }
    if (config.volume.has_value()) {
        mHfpConfig.volume = config.volume;
    }
    *result = mHfpConfig;
    return ndk::ScopedAStatus::ok();
}

}
