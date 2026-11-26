/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

namespace mindone::camera {

bool fixCharacteristics(std::vector<uint8_t>* metadata, bool maximumResolution, bool ois);

}
