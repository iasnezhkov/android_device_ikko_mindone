/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/soundtrigger3/BnSoundTriggerHw.h>
#include <memory>
#include <optional>
#include <vector>

namespace aidl::android::hardware::soundtrigger3::mindone {

namespace st = ::aidl::android::media::soundtrigger;

class SoundTriggerMindone final : public BnSoundTriggerHw {
  public:
    struct Impl;
    SoundTriggerMindone();
    ~SoundTriggerMindone() override;
    bool initialize();
    void setCaptureBlocked(bool blocked);
    ndk::ScopedAStatus getProperties(st::Properties* result) override;
    ndk::ScopedAStatus registerGlobalCallback(
            const std::shared_ptr<ISoundTriggerHwGlobalCallback>& callback) override;
    ndk::ScopedAStatus loadSoundModel(const st::SoundModel& model,
            const std::shared_ptr<ISoundTriggerHwCallback>& callback, int32_t* handle) override;
    ndk::ScopedAStatus loadPhraseSoundModel(const st::PhraseSoundModel& model,
            const std::shared_ptr<ISoundTriggerHwCallback>& callback, int32_t* handle) override;
    ndk::ScopedAStatus unloadSoundModel(int32_t handle) override;
    ndk::ScopedAStatus startRecognition(int32_t handle, int32_t deviceHandle, int32_t ioHandle,
            const st::RecognitionConfig& config) override;
    ndk::ScopedAStatus stopRecognition(int32_t handle) override;
    ndk::ScopedAStatus forceRecognitionEvent(int32_t handle) override;
    ndk::ScopedAStatus queryParameter(int32_t handle, st::ModelParameter parameter,
            std::optional<st::ModelParameterRange>* result) override;
    ndk::ScopedAStatus getParameter(int32_t handle, st::ModelParameter parameter,
            int32_t* result) override;
    ndk::ScopedAStatus setParameter(int32_t handle, st::ModelParameter parameter,
            int32_t value) override;

  private:
    std::shared_ptr<Impl> mImpl;
};

}
