// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_TILED_CAPTURE_BUFFER_H
#define ANDROID_V4L2_CODEC2_V4L2_TILED_CAPTURE_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace android {

class TiledCaptureBuffer {
public:
    TiledCaptureBuffer() = default;
    ~TiledCaptureBuffer();

    TiledCaptureBuffer(const TiledCaptureBuffer&) = delete;
    TiledCaptureBuffer& operator=(const TiledCaptureBuffer&) = delete;
    TiledCaptureBuffer(TiledCaptureBuffer&& other) noexcept;
    TiledCaptureBuffer& operator=(TiledCaptureBuffer&& other) noexcept;

    bool allocate(size_t size);
    void release();
    bool copyFrom(const uint8_t* data, size_t size, size_t capacity);

    int fd() const { return mFd; }
    const uint8_t* data() const { return mMapping; }
    size_t size() const { return mSize; }
    bool isValid() const { return mFd >= 0 && mMapping != nullptr; }

    bool syncStartRead() const;
    bool syncEndRead() const;

private:
    bool allocate(size_t size, bool writable);
    bool mWritable = false;
    int mFd = -1;
    uint8_t* mMapping = nullptr;
    size_t mSize = 0;
};

class TiledCaptureBufferPool {
public:
    TiledCaptureBufferPool() = default;
    ~TiledCaptureBufferPool() = default;

    TiledCaptureBufferPool(const TiledCaptureBufferPool&) = delete;
    TiledCaptureBufferPool& operator=(const TiledCaptureBufferPool&) = delete;

    bool allocate(size_t count, size_t bufferSize);
    void reset();

    TiledCaptureBuffer* bufferAt(size_t index);
    size_t count() const { return mBuffers.size(); }

private:
    std::vector<TiledCaptureBuffer> mBuffers;
};

}

#endif
