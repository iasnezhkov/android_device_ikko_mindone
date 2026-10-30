// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "TiledCaptureBuffer"

#include <v4l2_codec2/v4l2/TiledCaptureBuffer.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <log/log.h>

namespace android {

namespace {

constexpr const char* kDmaHeapCandidates[] = {
        "/dev/dma_heap/system",
        "/dev/dma_heap/mtk_mm",
        "/dev/dma_heap/mtk_mm-uncached",
};

int allocFromAnyHeap(size_t size) {
    for (const char* heapPath : kDmaHeapCandidates) {
        int heapFd = open(heapPath, O_RDONLY | O_CLOEXEC);
        if (heapFd < 0) {
            continue;
        }

        struct dma_heap_allocation_data data = {};
        data.len = size;
        data.fd_flags = O_RDWR | O_CLOEXEC;
        data.heap_flags = 0;

        const int ret = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &data);
        close(heapFd);
        if (ret < 0) {
            continue;
        }
        return static_cast<int>(data.fd);
    }
    ALOGE("Failed to allocate %zu bytes from any DMA-BUF heap", size);
    return -1;
}

bool syncDmaBuf(int fd, uint64_t flags) {
    struct dma_buf_sync sync = {};
    sync.flags = flags;
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) != 0) {
        ALOGE("DMA_BUF_IOCTL_SYNC failed on fd %d, flags=0x%llx", fd,
              static_cast<unsigned long long>(flags));
        return false;
    }
    return true;
}

}

TiledCaptureBuffer::~TiledCaptureBuffer() {
    release();
}

TiledCaptureBuffer::TiledCaptureBuffer(TiledCaptureBuffer&& other) noexcept
      : mWritable(other.mWritable), mFd(other.mFd), mMapping(other.mMapping), mSize(other.mSize) {
    other.mWritable = false;
    other.mFd = -1;
    other.mMapping = nullptr;
    other.mSize = 0;
}

TiledCaptureBuffer& TiledCaptureBuffer::operator=(TiledCaptureBuffer&& other) noexcept {
    if (this != &other) {
        release();
        mWritable = other.mWritable;
        mFd = other.mFd;
        mMapping = other.mMapping;
        mSize = other.mSize;
        other.mWritable = false;
        other.mFd = -1;
        other.mMapping = nullptr;
        other.mSize = 0;
    }
    return *this;
}

bool TiledCaptureBuffer::allocate(size_t size) {
    return allocate(size, false);
}

bool TiledCaptureBuffer::allocate(size_t size, bool writable) {
    release();
    if (size == 0) {
        return false;
    }

    const int fd = allocFromAnyHeap(size);
    if (fd < 0) {
        return false;
    }

    void* addr = mmap(nullptr, size, PROT_READ | (writable ? PROT_WRITE : 0), MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        ALOGE("mmap failed for tiled CAPTURE buffer of %zu bytes: %s", size, strerror(errno));
        close(fd);
        return false;
    }

    mWritable = writable;
    mFd = fd;
    mMapping = static_cast<uint8_t*>(addr);
    mSize = size;
    return true;
}

void TiledCaptureBuffer::release() {
    if (mMapping) {
        munmap(mMapping, mSize);
        mMapping = nullptr;
    }
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
    mSize = 0;
    mWritable = false;
}

bool TiledCaptureBuffer::copyFrom(const uint8_t* data, size_t size, size_t capacity) {
    if (!data || size == 0 || size > capacity) {
        return false;
    }
    if ((!isValid() || !mWritable || mSize != capacity) && !allocate(capacity, true)) {
        return false;
    }
    if (!syncDmaBuf(mFd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE)) {
        return false;
    }
    memcpy(mMapping, data, size);
    return syncDmaBuf(mFd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
}

bool TiledCaptureBuffer::syncStartRead() const {
    return isValid() && syncDmaBuf(mFd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
}

bool TiledCaptureBuffer::syncEndRead() const {
    return isValid() && syncDmaBuf(mFd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
}

bool TiledCaptureBufferPool::allocate(size_t count, size_t bufferSize) {
    reset();

    std::vector<TiledCaptureBuffer> buffers(count);
    for (size_t i = 0; i < count; i++) {
        if (!buffers[i].allocate(bufferSize)) {
            ALOGE("Failed to allocate tiled CAPTURE buffer %zu/%zu", i, count);
            return false;
        }
    }
    mBuffers = std::move(buffers);
    return true;
}

void TiledCaptureBufferPool::reset() {
    mBuffers.clear();
}

TiledCaptureBuffer* TiledCaptureBufferPool::bufferAt(size_t index) {
    if (index >= mBuffers.size()) {
        return nullptr;
    }
    return &mBuffers[index];
}

}
