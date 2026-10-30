/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ModuleMindone.h"

#include <algorithm>
#include <utility>

#include <android-base/logging.h>
#include <media/AidlConversionCppNdk.h>
#include <media/AidlConversionNdk.h>

#include "StreamMindone.h"
#include "TelephonyMindone.h"

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioMode;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::AudioPortExt;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core::mindone {

ModuleMindone::ModuleMindone(std::unique_ptr<Module::Configuration>&& config,
                             std::shared_ptr<LegacyBackend> backend)
    : Module(Module::Type::DEFAULT, std::move(config)), mBackend(std::move(backend)) {}

ndk::ScopedAStatus ModuleMindone::createInputStream(
        StreamContext&& context, const SinkMetadata& sinkMetadata,
        const std::vector<MicrophoneInfo>& microphones, std::shared_ptr<StreamIn>* result) {
    return createStreamInstance<StreamInMindone>(result, std::move(context), sinkMetadata,
                                                microphones, mBackend);
}

ndk::ScopedAStatus ModuleMindone::createOutputStream(
        StreamContext&& context, const SourceMetadata& sourceMetadata,
        const std::optional<AudioOffloadInfo>& offloadInfo, std::shared_ptr<StreamOut>* result) {
    return createStreamInstance<StreamOutMindone>(result, std::move(context), sourceMetadata,
                                                 offloadInfo, mBackend);
}

ndk::ScopedAStatus ModuleMindone::getTelephony(std::shared_ptr<ITelephony>* telephony) {
    std::lock_guard lock(mMutex);
    if (!mTelephony) {
        mTelephony = ndk::SharedRefBase::make<TelephonyMindone>(mBackend);
    }
    *telephony = mTelephony.getInstance();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ModuleMindone::setAudioPatch(const AudioPatch& in_requested,
                                              AudioPatch* _aidl_return) {
    std::lock_guard lock(mMutex);
    std::optional<AudioPatch> previous;
    for (const auto& patch : getConfig().patches) {
        if (patch.id == in_requested.id) previous = patch;
    }
    auto status = Module::setAudioPatch(in_requested, _aidl_return);
    if (!status.isOk()) return status;

    const auto& configs = getConfig().portConfigs;
    const auto findConfig = [&](int32_t id) {
        return std::find_if(configs.begin(), configs.end(),
                            [id](const AudioPortConfig& config) { return config.id == id; });
    };
    const auto isDevice = [&](int32_t id) {
        const auto config = findConfig(id);
        return config != configs.end() && config->ext.getTag() == AudioPortExt::device;
    };
    const auto convert = [&](const std::vector<int32_t>& ids, bool isInput,
                             std::vector<audio_port_config>* ports) {
        ports->clear();
        for (int32_t id : ids) {
            const auto config = findConfig(id);
            if (config == configs.end() || config->ext.getTag() != AudioPortExt::device) {
                return false;
            }
            audio_port_config port = {};
            int32_t portId;
            if (::aidl::android::aidl2legacy_AudioPortConfig_audio_port_config(
                        *config, isInput, &port, &portId) != 0) {
                return false;
            }
            ports->push_back(port);
        }
        return true;
    };
    bool releasedLegacy = false;
    const auto rollback = [&]() {
        if (previous.has_value()) {
            AudioPatch restored;
            if (!Module::setAudioPatch(*previous, &restored).isOk()) {
                LOG(ERROR) << "Failed to restore audio patch " << previous->id;
            }
            if (releasedLegacy) {
                std::vector<audio_port_config> sources;
                std::vector<audio_port_config> sinks;
                audio_patch_handle_t handle = AUDIO_PATCH_HANDLE_NONE;
                if (convert(previous->sourcePortConfigIds, true, &sources) &&
                    convert(previous->sinkPortConfigIds, false, &sinks) &&
                    mBackend->createAudioPatch(sources, sinks, &handle) == 0) {
                    mLegacyPatches[previous->id] = handle;
                } else {
                    LOG(ERROR) << "Failed to restore legacy audio patch " << previous->id;
                }
            }
        } else if (!Module::resetAudioPatch(_aidl_return->id).isOk()) {
            LOG(ERROR) << "Failed to roll back audio patch " << _aidl_return->id;
        }
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    };
    const bool devicePatch =
            std::all_of(_aidl_return->sourcePortConfigIds.begin(),
                        _aidl_return->sourcePortConfigIds.end(), isDevice) &&
            std::all_of(_aidl_return->sinkPortConfigIds.begin(),
                        _aidl_return->sinkPortConfigIds.end(), isDevice);
    std::vector<audio_port_config> sources;
    std::vector<audio_port_config> sinks;
    if (devicePatch && (!convert(_aidl_return->sourcePortConfigIds, true, &sources) ||
                        !convert(_aidl_return->sinkPortConfigIds, false, &sinks))) {
        return rollback();
    }
    auto existing = mLegacyPatches.find(_aidl_return->id);
    if (existing != mLegacyPatches.end()) {
        if (mBackend->releaseAudioPatch(existing->second) != 0) return rollback();
        mLegacyPatches.erase(existing);
        releasedLegacy = true;
    }
    if (devicePatch) {
        audio_patch_handle_t handle = AUDIO_PATCH_HANDLE_NONE;
        if (mBackend->createAudioPatch(sources, sinks, &handle) != 0) return rollback();
        mLegacyPatches[_aidl_return->id] = handle;
    }
    return status;
}

ndk::ScopedAStatus ModuleMindone::resetAudioPatch(int32_t in_patchId) {
    std::lock_guard lock(mMutex);
    int legacyStatus = 0;
    auto existing = mLegacyPatches.find(in_patchId);
    if (existing != mLegacyPatches.end()) {
        legacyStatus = mBackend->releaseAudioPatch(existing->second);
    }
    auto status = Module::resetAudioPatch(in_patchId);
    mLegacyPatches.erase(in_patchId);
    if (!status.isOk()) return status;
    return legacyStatus == 0 ? ndk::ScopedAStatus::ok()
                             : ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
}

ndk::ScopedAStatus ModuleMindone::getMicMute(bool* muted) {
    return mBackend->getMicMute(muted) == 0
                   ? ndk::ScopedAStatus::ok()
                   : ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
}

ndk::ScopedAStatus ModuleMindone::setMicMute(bool muted) {
    return mBackend->setMicMute(muted) == 0
                   ? ndk::ScopedAStatus::ok()
                   : ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
}

ndk::ScopedAStatus ModuleMindone::getMasterMute(bool*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus ModuleMindone::setMasterMute(bool) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus ModuleMindone::getMasterVolume(float*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus ModuleMindone::setMasterVolume(float) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus ModuleMindone::updateAudioMode(AudioMode mode) {
    return setAudioMode(*mBackend, mode);
}

}
