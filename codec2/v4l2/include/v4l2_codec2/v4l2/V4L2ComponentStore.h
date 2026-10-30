// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_COMPONENT_STORE_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_COMPONENT_STORE_H

#include <C2Component.h>

namespace android {

struct V4L2ComponentStore {
    static std::shared_ptr<C2ComponentStore> Create();
};

}

#endif
