/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/hardware/gnss/2.1/IGnss.h>

namespace mindone::gnss {

::android::sp<::android::hardware::gnss::V2_1::IGnss> loadBackend();

}
