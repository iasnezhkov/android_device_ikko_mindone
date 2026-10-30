/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/dumpstate/BnDumpstateDevice.h>

namespace aidl::android::hardware::dumpstate {

class Dumpstate final : public BnDumpstateDevice {
  public:
    ndk::ScopedAStatus dumpstateBoard(const std::vector<ndk::ScopedFileDescriptor>& in_fds,
                                      IDumpstateDevice::DumpstateMode in_mode,
                                      int64_t in_timeoutMillis) override;
    ndk::ScopedAStatus getVerboseLoggingEnabled(bool* _aidl_return) override;
    ndk::ScopedAStatus setVerboseLoggingEnabled(bool in_enable) override;
};

}
