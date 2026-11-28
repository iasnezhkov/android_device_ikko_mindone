/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <mutex>

#include <aidl/android/hardware/audio/core/BnBluetooth.h>

#include "Backend.h"

namespace aidl::android::hardware::audio::core::mindone {

class BluetoothMindone : public BnBluetooth {
  public:
    explicit BluetoothMindone(std::shared_ptr<LegacyBackend> backend);

    ndk::ScopedAStatus setScoConfig(const ScoConfig& config, ScoConfig* result) override;
    ndk::ScopedAStatus setHfpConfig(const HfpConfig& config, HfpConfig* result) override;

  private:
    std::shared_ptr<LegacyBackend> mBackend;
    ScoConfig mScoConfig;
    HfpConfig mHfpConfig;
    std::mutex mMutex;
};

}
