/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sstream>

namespace mindone::test::stub {

struct NullLogStream {
    template <typename T>
    NullLogStream& operator<<(const T&) {
        return *this;
    }
};

}

#define LOG(severity) ::mindone::test::stub::NullLogStream()
