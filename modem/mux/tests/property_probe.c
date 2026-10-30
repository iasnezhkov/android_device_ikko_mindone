/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

int mindone_mux_set_property(uint32_t property, uint32_t value);

int main(void)
{
    unsigned accepted = 0, rejected = 0;
    for (uint32_t key = 0; key < 9; ++key) {
        for (uint32_t value = 0; value < 4; ++value) {
            int valid = (key == 0 && value == 0) || (key == 1 && value <= 1) ||
                (key >= 2 && key <= 4 && value == 1) ||
                (key == 5 && (value == 1 || value == 2)) || ((key == 6 || key == 7) && value == 1);
            int result = mindone_mux_set_property(key, value);
            if (!valid) {
                if (result != -EINVAL) return 1;
                ++rejected;
            } else {
#ifdef EXPECT_UNSUPPORTED
                if (result != -ENOTSUP) return 2;
#else
                if (result != 0) return 3;
#endif
                ++accepted;
            }
        }
    }
    if (mindone_mux_set_property(UINT32_MAX, 0) != -EINVAL ||
        mindone_mux_set_property(0, UINT32_MAX) != -EINVAL) return 4;
    printf("PROPERTY_MATRIX_PASS allowed=%u rejected=%u\n", accepted, rejected + 2);
    return 0;
}
