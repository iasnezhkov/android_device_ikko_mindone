/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mind_one minimal RIL, phase 1 skeleton (RIL-MINIMAL-1409).
//
// main() for android.hardware.radio@1.6-service.mindone -- registers instance "slot1" only.
// Modeled on the real AOSP reference implementation
// hardware/interfaces/radio/1.2/default/radio-service.cpp (present read-only in the LineageOS
// LineageOS tree; that file registers its own "default" test/mock IRadio the same way, with
// registerAsService() taking no instance name -- we pass "slot1" explicitly since that's the
// instance the telephony framework actually resolves against on this device, per
// device/ikko/mindone/manifest.xml, cited in RadioImpl.h).
//
// Disabled by default (see init.mindone_ril.rc) -- start manually for an A/B test on the rescue
// slot per RIL-MINIMAL-1409's A/B plan, after `stop vendor.ril-daemon-mtk` and the mux
// channels are confirmed present (`ls -la /dev/radio/pttycmd1 /dev/radio/pttynoti`).
#include <hidl/HidlTransportSupport.h>
#include <cutils/properties.h>
#include <unistd.h>

#undef LOG_TAG
#define LOG_TAG "mindone_ril_service"
#include <log/log.h>

#include "RadioImpl.h"

using ::android::OK;
using ::android::sp;
using ::android::status_t;
using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::joinRpcThreadpool;
using mindone::ril::MindoneRadio;

namespace {
constexpr int kOpenRetries = 20;
constexpr int kOpenRetryDelayUs = 500 * 1000;  // 500ms; ~10s total, matches gsm0710muxd's own
                                                // channel-establishment window observed live
                                                // (RIL-MINIMAL-1409 "channel layout")
}  // namespace

int main() {
    char cmdDev[PROPERTY_VALUE_MAX];
    char notiDev[PROPERTY_VALUE_MAX];
    // Overridable for bring-up/debugging without a rebuild -- see README-INTEGRATION.md.
    property_get("persist.vendor.mindone_ril.cmd_dev", cmdDev, "/dev/radio/pttycmd1");
    property_get("persist.vendor.mindone_ril.noti_dev", notiDev, "/dev/radio/pttynoti");

    sp<MindoneRadio> radio = new MindoneRadio();

    bool opened = false;
    for (int i = 0; i < kOpenRetries && !opened; i++) {
        if (i > 0) usleep(kOpenRetryDelayUs);
        opened = radio->openAtChannels(cmdDev, notiDev);
        if (!opened) {
            ALOGW("waiting for AT channels (%s, %s), attempt %d/%d", cmdDev, notiDev, i + 1,
                  kOpenRetries);
        }
    }
    if (!opened) {
        ALOGE("could not open AT channels after %d attempts, exiting (init will restart us)",
              kOpenRetries);
        return 1;
    }

    configureRpcThreadpool(1, true);
    status_t status = radio->registerAsService("slot1");
    if (status != OK) {
        ALOGE("registerAsService(slot1) failed: %d", status);
        return 1;
    }
    ALOGI("android.hardware.radio@1.6::IRadio/slot1 registered, entering threadpool");
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool() should never return
}
