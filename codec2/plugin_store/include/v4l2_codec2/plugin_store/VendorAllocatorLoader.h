// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_PLUGIN_STORE_VENDOR_ALLOCATOR_LOADER_H
#define ANDROID_V4L2_CODEC2_PLUGIN_STORE_VENDOR_ALLOCATOR_LOADER_H

#include <memory>
#include <mutex>

#include <C2Buffer.h>

namespace android {

class VendorAllocatorLoader {
public:
    using CreateAllocatorFunc = ::C2Allocator* (*)(C2Allocator::id_t  );
    using CreateBlockPoolFunc = ::C2BlockPool* (*)(C2Allocator::id_t  ,
                                                   C2BlockPool::local_id_t  );

    static std::unique_ptr<VendorAllocatorLoader> Create();
    ~VendorAllocatorLoader();

    C2Allocator* createAllocator(C2Allocator::id_t allocatorId);

    C2BlockPool* createBlockPool(C2Allocator::id_t allocatorId, C2BlockPool::local_id_t poolId);

private:
    VendorAllocatorLoader(void* libHandle, CreateAllocatorFunc createAllocatorFunc,
                          CreateBlockPoolFunc createBlockPoolFunc);

    void* mLibHandle;
    CreateAllocatorFunc mCreateAllocatorFunc;
    CreateBlockPoolFunc mCreateBlockPoolFunc;
};

}
#endif
