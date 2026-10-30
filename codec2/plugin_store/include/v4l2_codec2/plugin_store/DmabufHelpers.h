// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_PLUGIN_STORE_DMABUF_HELPERS_H
#define ANDROID_V4L2_CODEC2_PLUGIN_STORE_DMABUF_HELPERS_H

#include <inttypes.h>

#include <optional>

namespace android {

using unique_id_t = uint32_t;
std::optional<unique_id_t> getDmabufId(int dmabufFd);

}

#endif
