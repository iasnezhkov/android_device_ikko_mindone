// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_PLUGIN_STORE_V4L2_ALLOCATOR_ID_H
#define ANDROID_V4L2_CODEC2_PLUGIN_STORE_V4L2_ALLOCATOR_ID_H

#include <C2PlatformSupport.h>

namespace android {
namespace V4L2AllocatorId {

enum : C2AllocatorStore::id_t {
    SECURE_LINEAR = C2PlatformAllocatorStore::PLATFORM_END,
    SECURE_GRAPHIC,
};

}
}

#endif
