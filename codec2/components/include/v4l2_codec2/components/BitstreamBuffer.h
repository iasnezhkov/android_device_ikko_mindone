// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_BITSTREAMBUFFER_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_BITSTREAMBUFFER_H

#include <stdint.h>

#include <C2Buffer.h>

namespace android {

struct ConstBitstreamBuffer {
    ConstBitstreamBuffer(const int32_t id, C2ConstLinearBlock dmabuf, const size_t offset,
                         const size_t size)
          : id(id), dmabuf(std::move(dmabuf)), offset(offset), size(size) {}
    ~ConstBitstreamBuffer() = default;

    const int32_t id;
    C2ConstLinearBlock dmabuf;
    const size_t offset;
    const size_t size;
};

struct BitstreamBuffer {
    BitstreamBuffer(std::shared_ptr<C2LinearBlock> dmabuf, const size_t offset, const size_t size)
          : dmabuf(std::move(dmabuf)), offset(offset), size(size) {}
    ~BitstreamBuffer() = default;

    std::shared_ptr<C2LinearBlock> dmabuf;
    const size_t offset;
    const size_t size;
};

}

#endif
