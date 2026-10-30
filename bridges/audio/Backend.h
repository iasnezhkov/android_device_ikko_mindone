/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "AudioBackend.h"

namespace aidl::android::hardware::audio::core::mindone {

class LegacyBackend {
  public:
    static std::shared_ptr<LegacyBackend> open();
    ~LegacyBackend();

    mindone_audio_device* device() { return mDevice; }
    int setMode(audio_mode_t mode);
    int setVoiceVolume(float volume);
    int setMicMute(bool muted);
    int getMicMute(bool* muted);
    int createAudioPatch(const std::vector<audio_port_config>& sources,
                         const std::vector<audio_port_config>& sinks, audio_patch_handle_t* handle);
    int releaseAudioPatch(audio_patch_handle_t handle);

  private:
    explicit LegacyBackend(mindone_audio_device* device) : mDevice(device) {}

    mindone_audio_device* const mDevice;
    std::mutex mMutex;
};

}
