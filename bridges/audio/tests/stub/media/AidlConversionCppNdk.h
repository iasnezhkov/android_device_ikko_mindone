/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "core-impl/Module.h"
#include "system/audio.h"

namespace aidl::android {

inline int aidl2legacy_AudioPortConfig_audio_port_config(
        const ::aidl::android::media::audio::common::AudioPortConfig&, bool,
        audio_port_config*, int32_t*) {
    return 0;
}

}
