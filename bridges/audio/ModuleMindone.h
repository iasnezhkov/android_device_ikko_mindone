/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "Backend.h"
#include "core-impl/Module.h"

namespace aidl::android::hardware::audio::core::mindone {

class ModuleMindone : public Module {
  public:
    ModuleMindone(std::unique_ptr<Module::Configuration>&& config,
                  std::shared_ptr<LegacyBackend> backend);

  protected:
    ndk::ScopedAStatus createInputStream(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones,
            std::shared_ptr<StreamIn>* result) override;
    ndk::ScopedAStatus createOutputStream(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>& offloadInfo,
            std::shared_ptr<StreamOut>* result) override;
    ndk::ScopedAStatus getTelephony(std::shared_ptr<ITelephony>* telephony) override;
    ndk::ScopedAStatus setAudioPatch(const AudioPatch& in_requested,
                                   AudioPatch* _aidl_return) override;
    ndk::ScopedAStatus resetAudioPatch(int32_t in_patchId) override;
    ndk::ScopedAStatus getMicMute(bool* muted) override;
    ndk::ScopedAStatus setMicMute(bool muted) override;
    ndk::ScopedAStatus getMasterMute(bool* muted) override;
    ndk::ScopedAStatus setMasterMute(bool muted) override;
    ndk::ScopedAStatus getMasterVolume(float* volume) override;
    ndk::ScopedAStatus setMasterVolume(float volume) override;
    ndk::ScopedAStatus updateAudioMode(
            ::aidl::android::media::audio::common::AudioMode mode) override;

  private:
    std::shared_ptr<LegacyBackend> mBackend;
    ChildInterface<ITelephony> mTelephony;
    std::mutex mMutex;
    std::map<int32_t, audio_patch_handle_t> mLegacyPatches;
};

}
