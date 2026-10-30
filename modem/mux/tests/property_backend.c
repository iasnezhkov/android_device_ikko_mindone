/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <stdio.h>
#include <stdlib.h>

static unsigned calls;

int __system_property_set(const char *key, const char *value)
{
    const char *failure = getenv("MINDONE_PROPERTY_FAILURE");
    unsigned rejected = failure ? (unsigned)strtoul(failure, NULL, 10) : 0;
    ++calls;
    printf("property %s=%s%s\n", key, value, calls == rejected ? " rejected" : "");
    fflush(stdout);
    return calls == rejected ? -37 : 0;
}
