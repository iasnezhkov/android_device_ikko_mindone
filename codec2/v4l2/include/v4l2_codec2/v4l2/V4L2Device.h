// Copyright 2014 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_DEVICE_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_DEVICE_H

#include <linux/videodev2.h>
#include <stddef.h>
#include <stdint.h>

#include <cstdint>
#include <optional>
#include <vector>

#include <C2Config.h>
#include <base/containers/flat_map.h>
#include <base/files/scoped_file.h>
#include <base/memory/ref_counted.h>

#include <ui/Size.h>
#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/VideoTypes.h>
#include <v4l2_codec2/v4l2/V4L2DevicePoller.h>

#ifndef V4L2_PIX_FMT_VP8_FRAME
#define V4L2_PIX_FMT_VP8_FRAME v4l2_fourcc('V', 'P', '8', 'F')
#endif

#ifndef V4L2_PIX_FMT_VP9_FRAME
#define V4L2_PIX_FMT_VP9_FRAME v4l2_fourcc('V', 'P', '9', 'F')
#endif

#ifndef V4L2_PIX_FMT_H264_SLICE
#define V4L2_PIX_FMT_H264_SLICE v4l2_fourcc('S', '2', '6', '4')
#endif

#ifndef V4L2_PIX_FMT_HEVC_SLICE
#define V4L2_PIX_FMT_HEVC_SLICE v4l2_fourcc('S', '2', '6', '5')
#endif

#ifndef V4L2_PIX_FMT_AV1_FRAME
#define V4L2_PIX_FMT_AV1_FRAME v4l2_fourcc('A', 'V', '1', 'F')
#endif
#ifndef V4L2_PIX_FMT_AV1
#define V4L2_PIX_FMT_AV1 v4l2_fourcc('A', 'V', '0', '1')
#endif

namespace android {

class V4L2Queue;
class V4L2BufferRefBase;
class V4L2BuffersList;
class V4L2DecodeSurface;

struct V4L2ExtCtrl {
    V4L2ExtCtrl(uint32_t id);
    V4L2ExtCtrl(uint32_t id, int32_t val);
    V4L2ExtCtrl(uint32_t id, void* payload, uint32_t payloadSize);
    struct v4l2_ext_control ctrl;
};

bool isValidPixFmtForCodec(VideoCodec codec, uint32_t pixFmt);

class V4L2WritableBufferRef {
public:
    V4L2WritableBufferRef(V4L2WritableBufferRef&& other);
    V4L2WritableBufferRef() = delete;
    V4L2WritableBufferRef& operator=(V4L2WritableBufferRef&& other);

    enum v4l2_memory memory() const;

    bool queueMMap() &&;

    bool queueUserPtr(const std::vector<void*>& ptrs) &&;

    bool queueDMABuf(const std::vector<int>& fds) &&;

    size_t planesCount() const;

    size_t getPlaneSize(const size_t plane) const;

    void setPlaneSize(const size_t plane, const size_t size);

    void* getPlaneMapping(const size_t plane);

    void setTimeStamp(const struct timeval& timestamp);

    const struct timeval& getTimeStamp() const;

    void setPlaneBytesUsed(const size_t plane, const size_t bytesUsed);

    size_t getPlaneBytesUsed(const size_t plane) const;

    void setPlaneDataOffset(const size_t plane, const size_t dataOffset);

    size_t bufferId() const;

    ~V4L2WritableBufferRef();

private:
    friend class V4L2BufferRefFactory;

    bool doQueue() &&;

    V4L2WritableBufferRef(const struct v4l2_buffer& v4l2Buffer, ::base::WeakPtr<V4L2Queue> queue);

    V4L2WritableBufferRef(const V4L2WritableBufferRef&) = delete;
    V4L2WritableBufferRef& operator=(const V4L2WritableBufferRef&) = delete;

    std::unique_ptr<V4L2BufferRefBase> mBufferData;

    SEQUENCE_CHECKER(mSequenceChecker);
};

class V4L2ReadableBuffer : public ::base::RefCountedThreadSafe<V4L2ReadableBuffer> {
public:

    bool isLast() const;

    bool isKeyframe() const;

    struct timeval getTimeStamp() const;

    size_t planesCount() const;

    size_t getPlaneBytesUsed(size_t plane) const;

    size_t getPlaneDataOffset(size_t plane) const;

    const void* getPlaneMapping(const size_t plane) const;

    size_t bufferId() const;

private:
    friend class V4L2BufferRefFactory;
    friend class ::base::RefCountedThreadSafe<V4L2ReadableBuffer>;

    ~V4L2ReadableBuffer();

    V4L2ReadableBuffer(const struct v4l2_buffer& v4l2Buffer, ::base::WeakPtr<V4L2Queue> queue);

    V4L2ReadableBuffer(const V4L2ReadableBuffer&) = delete;
    V4L2ReadableBuffer& operator=(const V4L2ReadableBuffer&) = delete;

    std::unique_ptr<V4L2BufferRefBase> mBufferData;

    SEQUENCE_CHECKER(mSequenceChecker);
};

using V4L2ReadableBufferRef = scoped_refptr<V4L2ReadableBuffer>;

class V4L2Device;
class V4L2Buffer;

class V4L2Queue : public ::base::RefCountedThreadSafe<V4L2Queue> {
public:

    std::optional<struct v4l2_format> setFormat(uint32_t fourcc, const ui::Size& size,
                                                size_t bufferSize,
                                                uint32_t stride = 0) WARN_UNUSED_RESULT;

    std::optional<struct v4l2_format> tryFormat(uint32_t fourcc, const ui::Size& size,
                                                size_t bufferSize) WARN_UNUSED_RESULT;

    std::pair<std::optional<struct v4l2_format>, int> getFormat();

    size_t allocateBuffers(size_t count, enum v4l2_memory memory) WARN_UNUSED_RESULT;

    bool deallocateBuffers();

    size_t getMemoryUsage() const;

    v4l2_memory getMemoryType() const;

    std::optional<V4L2WritableBufferRef> getFreeBuffer();
    std::optional<V4L2WritableBufferRef> getFreeBuffer(size_t requestedBufferId);

    std::pair<bool, V4L2ReadableBufferRef> dequeueBuffer();

    bool isStreaming() const;

    bool streamon();

    bool streamoff();

    size_t allocatedBuffersCount() const;

    size_t freeBuffersCount() const;

    size_t queuedBuffersCount() const;

private:
    ~V4L2Queue();

    V4L2Queue(const V4L2Queue&) = delete;
    V4L2Queue& operator=(const V4L2Queue&) = delete;

    bool queueBuffer(struct v4l2_buffer* v4l2Buffer);

    void reportTraceMetrics();

    const enum v4l2_buf_type mType;
    enum v4l2_memory mMemory = V4L2_MEMORY_MMAP;
    bool mIsStreaming = false;
    size_t mPlanesCount = 0;

    std::optional<struct v4l2_format> mCurrentFormat;

    std::vector<std::unique_ptr<V4L2Buffer>> mBuffers;

    scoped_refptr<V4L2BuffersList> mFreeBuffers;

    std::set<size_t> mQueuedBuffers;

    scoped_refptr<V4L2Device> mDevice;

    ::base::OnceClosure mDestroyCb;

    V4L2Queue(scoped_refptr<V4L2Device> dev, enum v4l2_buf_type type,
              ::base::OnceClosure destroyCb);
    friend class V4L2QueueFactory;
    friend class V4L2BufferRefBase;
    friend class ::base::RefCountedThreadSafe<V4L2Queue>;

    SEQUENCE_CHECKER(mSequenceChecker);

    ::base::WeakPtrFactory<V4L2Queue> mWeakThisFactory{this};
};

class V4L2Device : public ::base::RefCountedThreadSafe<V4L2Device> {
public:

    static uint32_t c2ProfileToV4L2PixFmt(C2Config::profile_t profile, bool sliceBased);
    static C2Config::level_t v4L2LevelToC2Level(VideoCodec codec, uint32_t level);
    static C2Config::profile_t v4L2ProfileToC2Profile(VideoCodec codec, uint32_t profile);
    static uint32_t videoCodecToPixFmt(VideoCodec codec);

    static ui::Size allocatedSizeFromV4L2Format(const struct v4l2_format& format);

    static int32_t c2ProfileToV4L2H264Profile(C2Config::profile_t profile);
    static int32_t h264LevelIdcToV4L2H264Level(uint8_t levelIdc);
    static v4l2_mpeg_video_bitrate_mode c2BitrateModeToV4L2BitrateMode(
            C2Config::bitrate_mode_t bitrateMode);

    static const char* v4L2MemoryToString(const v4l2_memory memory);

    static const char* v4L2BufferTypeToString(const enum v4l2_buf_type bufType);

    static std::string v4L2BufferTypeToATraceLabel(uint32_t debugStreamId,
                                                   const enum v4l2_buf_type type,
                                                   const char* label);

    static std::string v4L2FormatToString(const struct v4l2_format& format);

    static std::string v4L2BufferToString(const struct v4l2_buffer& buffer);

    static std::optional<VideoFrameLayout> v4L2FormatToVideoFrameLayout(
            const struct v4l2_format& format);

    static size_t getNumPlanesOfV4L2PixFmt(uint32_t pixFmt);

    enum class Type { kDecoder, kEncoder };

    static SupportedProfiles getSupportedProfiles(Type type,
                                                  const std::vector<uint32_t>& pixelFormats);

    static std::vector<C2Config::level_t> getSupportedDecodeLevels(VideoCodec videoCodecType);

    static C2Config::profile_t getDefaultProfile(VideoCodec codec);

    static C2Config::level_t getDefaultLevel(VideoCodec codec);

    static SupportedCapabilities queryDecodingCapabilities(VideoCodec codec);

    static SupportedCapabilities queryEncodingCapabilities(VideoCodec codec);

    static scoped_refptr<V4L2Device> create(uint32_t debugStreamId = -1);

    bool open(Type type, uint32_t v4l2PixFmt);

    scoped_refptr<V4L2Queue> getQueue(enum v4l2_buf_type type);

    int ioctl(int request, void* arg);

    bool poll(bool pollDevice, bool pollBuffers, bool* eventPending, bool* buffersPending);

    bool setDevicePollInterrupt();
    bool clearDevicePollInterrupt();

    void* mmap(void* addr, unsigned int len, int prot, int flags, unsigned int offset);
    void munmap(void* addr, unsigned int len);

    std::vector<::base::ScopedFD> getDmabufsForV4L2Buffer(int index, size_t numPlanes,
                                                          enum v4l2_buf_type bufType);

    std::vector<uint32_t> preferredInputFormat(Type type);

    void getSupportedResolution(uint32_t pixelFormat, ui::Size* minResolution,
                                ui::Size* maxResolution);

    std::vector<C2Config::level_t> queryC2Levels(uint32_t pixFmt);

    std::vector<C2Config::profile_t> queryC2Profiles(uint32_t pixFmt);

    std::vector<uint32_t> enumerateSupportedPixelformats(v4l2_buf_type bufType);

    std::vector<C2Config::level_t> enumerateSupportedDecodeLevels(VideoCodec videoCodecType);

    bool startPolling(scoped_refptr<::base::SequencedTaskRunner> taskRunner,
                      android::V4L2DevicePoller::EventCallback eventCallback,
                      ::base::RepeatingClosure errorCallback);

    bool stopPolling();

    void schedulePoll();

    bool isCtrlExposed(uint32_t ctrlId);

    bool setExtCtrls(uint32_t ctrlClass, std::vector<V4L2ExtCtrl> ctrls);

    bool isCommandSupported(uint32_t commandId);

    bool hasCapabilities(uint32_t capabilities);

    uint32_t getDebugStreamId() { return mDebugStreamId; }

private:

    using DeviceInfos = std::vector<std::pair<std::string, std::vector<uint32_t>>>;

    static const DeviceInfos& getDeviceInfosForType(V4L2Device::Type type);

    friend class ::base::RefCountedThreadSafe<V4L2Device>;
    V4L2Device(uint32_t debugStreamId);
    ~V4L2Device();

    V4L2Device(const V4L2Device&) = delete;
    V4L2Device& operator=(const V4L2Device&) = delete;

    SupportedProfiles enumerateSupportedProfiles(V4L2Device::Type type,
                                                 const std::vector<uint32_t>& pixelFormats);

    bool openDevicePath(const std::string& path, Type type);

    void closeDevice();

    std::string getDevicePathFor(V4L2Device::Type type, uint32_t pixFmt);

    void onQueueDestroyed(v4l2_buf_type buf_type);

    uint32_t mDebugStreamId;

    ::base::ScopedFD mDeviceFd;

    ::base::ScopedFD mDevicePollInterruptFd;

    ::base::flat_map<enum v4l2_buf_type, V4L2Queue*> mQueues;

    std::unique_ptr<android::V4L2DevicePoller> mDevicePoller;

    SEQUENCE_CHECKER(mClientSequenceChecker);
};

}

#endif
