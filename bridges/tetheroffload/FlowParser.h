/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace mindone::bridges::tetheroffload {
struct Flow {
    int family = 0;
    uint8_t proto = 0;
    std::array<uint8_t, 16> src{}, dst{}, pub{}, replySrc{};
    uint16_t sport = 0, dport = 0, publicPort = 0, replyPort = 0;
    uint32_t timeout = 0, status = 0, id = 0;
    bool destroy = false;
    std::array<uint8_t, 6> mac{};
    std::string key() const;
};
bool parseFlow(const void*, size_t, Flow*);
bool contains(const std::string&, int, const std::array<uint8_t, 16>&);
}
