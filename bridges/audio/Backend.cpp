/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Backend.h"

#include <cerrno>

#include <android-base/logging.h>

namespace aidl::android::hardware::audio::core::mindone {

std::shared_ptr<LegacyBackend> LegacyBackend::open() {
    mindone_audio_device* device = nullptr;
    const int status = mindone_audio_device_open(&device);
    if (status != 0 || device == nullptr) {
        LOG(ERROR) << "Failed to open primary audio backend: " << status;
        return nullptr;
    }
    return std::shared_ptr<LegacyBackend>(new LegacyBackend(device));
}

LegacyBackend::~LegacyBackend() {
    std::lock_guard lock(mMutex);
    const int status = mindone_audio_device_close(mDevice);
    if (status != 0) {
        LOG(ERROR) << "Failed to close primary audio device: " << status;
    }
}

int LegacyBackend::setMode(audio_mode_t mode) {
    std::lock_guard lock(mMutex);
    return mindone_audio_set_mode(mDevice, mode);
}

int LegacyBackend::setVoiceVolume(float volume) {
    std::lock_guard lock(mMutex);
    return mindone_audio_set_voice_volume(mDevice, volume);
}

int LegacyBackend::setMicMute(bool muted) {
    std::lock_guard lock(mMutex);
    return mindone_audio_set_mic_mute(mDevice, muted);
}

int LegacyBackend::getMicMute(bool* muted) {
    if (muted == nullptr) return -EINVAL;
    std::lock_guard lock(mMutex);
    mindone_audio_voice voice = {};
    const int status = mindone_audio_get_voice(mDevice, &voice);
    if (status == 0) *muted = voice.mic_mute;
    return status;
}

int LegacyBackend::createAudioPatch(const std::vector<audio_port_config>& sources,
                                    const std::vector<audio_port_config>& sinks,
                                    audio_patch_handle_t* handle) {
    std::lock_guard lock(mMutex);
    return mindone_audio_create_patch(mDevice, sources.size(), sources.data(),
                                      sinks.size(), sinks.data(), handle);
}

int LegacyBackend::releaseAudioPatch(audio_patch_handle_t handle) {
    std::lock_guard lock(mMutex);
    return mindone_audio_release_patch(mDevice, handle);
}

}
