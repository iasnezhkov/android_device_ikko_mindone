/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <errno.h>
#include <stdint.h>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

int mindone_mux_set_property(uint32_t property, uint32_t value)
{
    static const char *const keys[] = {
        "vendor.ril.muxreport.run", "vendor.ril.mux.ee.md1", "vendor.ril.mux.start",
        "vendor.ril.mtk", "vendor.lackof.atci.channel", "vendor.ril.mux.report.case", "vendor.ril.muxreport", "vendor.ril.mtk.restart"
    };
    static const char *const values[] = { "0", "1", "2" };
    if (property >= sizeof(keys) / sizeof(keys[0]) || value >= sizeof(values) / sizeof(values[0])) return -EINVAL;
    if ((property == 0 && value != 0) || (property == 1 && value > 1) ||
        (property >= 2 && property <= 4 && value != 1) ||
        (property == 5 && value != 1 && value != 2) || (property >= 6 && value != 1)) return -EINVAL;
#ifdef __ANDROID__
    return __system_property_set(keys[property], values[value]);
#else
    return -ENOTSUP;
#endif
}
