/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <aidl/android/hardware/audio/core/BnTelephony.h>

#include "Backend.h"

namespace aidl::android::hardware::audio::core::mindone {

ndk::ScopedAStatus setAudioMode(
        LegacyBackend& backend, ::aidl::android::media::audio::common::AudioMode mode);

class TelephonyMindone : public BnTelephony {
  public:
    explicit TelephonyMindone(std::shared_ptr<LegacyBackend> backend);

    ndk::ScopedAStatus getSupportedAudioModes(
            std::vector<::aidl::android::media::audio::common::AudioMode>* modes) override;
    ndk::ScopedAStatus switchAudioMode(
            ::aidl::android::media::audio::common::AudioMode mode) override;
    ndk::ScopedAStatus setTelecomConfig(const TelecomConfig& config,
                                      TelecomConfig* result) override;

  private:
    std::shared_ptr<LegacyBackend> mBackend;
    TelecomConfig mTelecomConfig;
    std::mutex mMutex;
};

}
