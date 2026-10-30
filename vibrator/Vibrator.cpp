/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <cstdio>
#include <thread>

#include <log/log.h>

#include <hardware/hardware.h>
#include <hardware/vibrator.h>

#include "Vibrator.h"

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

Vibrator::Vibrator() {
    vibrator_device_t *vib_device;
    const hw_module_t *hw_module = nullptr;

    int ret = hw_get_module(VIBRATOR_HARDWARE_MODULE_ID, &hw_module);
    if (ret == 0) {
        ret = vibrator_open(hw_module, &vib_device);
        if (ret != 0) {
            ALOGE("vibrator_open failed: %d", ret);
            return;
        }
    } else {
        ALOGE("hw_get_module %s failed: %d", VIBRATOR_HARDWARE_MODULE_ID, ret);
        return;
    }

    mDevice = vib_device;
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t* _aidl_return) {
    ALOGV("Vibrator reporting capabilities");
    *_aidl_return = IVibrator::CAP_ON_CALLBACK;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::off() {
    if (mDevice == nullptr) {
        ALOGE("off: vibrator device not opened");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    int32_t ret = mDevice->vibrator_off(mDevice);
    if (ret != 0) {
        ALOGE("off command failed : %s", strerror(-ret));
        return ndk::ScopedAStatus::fromExceptionCode(EX_TRANSACTION_FAILED);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs, const std::shared_ptr<IVibratorCallback>& callback) {
    if (mDevice == nullptr) {
        ALOGE("on: vibrator device not opened");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    int32_t ret = mDevice->vibrator_on(mDevice, timeoutMs);
    if (ret != 0) {
        ALOGE("on command failed : %s", strerror(-ret));
        return ndk::ScopedAStatus::fromExceptionCode(EX_TRANSACTION_FAILED);
    }
    if (callback != nullptr) {
        std::thread([callback, timeoutMs] {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
            callback->onComplete();
        }).detach();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::perform(Effect, EffectStrength, const std::shared_ptr<IVibratorCallback>&, int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect>*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::setAmplitude(float) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(std::vector<CompositePrimitive>*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive, int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect>&, const std::shared_ptr<IVibratorCallback>&) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(std::vector<Effect>*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t, Effect, EffectStrength ) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float* _aidl_return) {
    FILE* fp = fopen("/sys/class/leds/vibrator/f0", "r");
    if (fp == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    int raw = 0;
    int matched = fscanf(fp, "%d", &raw);
    fclose(fp);
    if (matched != 1 || raw <= 0) {
        ALOGE("getResonantFrequency: unexpected f0 content");
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    *_aidl_return = static_cast<float>(raw) / 10.0f;
    ALOGI("getResonantFrequency: %.1f Hz (raw %d)", *_aidl_return, raw);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getQFactor(float*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float>*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking>*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::composePwle(const std::vector<PrimitivePwle>&, const std::shared_ptr<IVibratorCallback>&) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}
}
}
}
