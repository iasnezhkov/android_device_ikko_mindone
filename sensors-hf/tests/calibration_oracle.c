/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void contract_calibration_payload(uint8_t sensor, uint8_t length, uint8_t action,
                                  const int32_t* words, uint8_t* output) {
    static const struct {
        uint8_t sensor;
        uint8_t length;
        uint8_t action;
        uint8_t offset;
        uint8_t count;
    } regions[] = {
        {1, 24, 2, 0, 3},
        {1, 24, 3, 12, 3},
        {2, 36, 2, 0, 3},
        {2, 36, 3, 12, 6},
        {4, 48, 2, 0, 3},
        {4, 48, 3, 12, 3},
        {4, 48, 4, 24, 6},
        {5, 4, 3, 0, 1},
    };
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        if (sensor == regions[i].sensor && length == regions[i].length &&
            action == regions[i].action) {
            memcpy(output + regions[i].offset, words, regions[i].count * sizeof(*words));
            return;
        }
    }
}
