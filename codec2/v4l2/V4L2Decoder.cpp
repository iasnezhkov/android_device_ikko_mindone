// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define ATRACE_TAG ATRACE_TAG_VIDEO
#define LOG_TAG "V4L2Decoder"

#include <v4l2_codec2/v4l2/V4L2Decoder.h>

#include <stdint.h>
#include <sys/stat.h>

#include <algorithm>
#include <limits>
#include <vector>

#include <base/bind.h>
#include <base/files/scoped_file.h>
#include <base/memory/ptr_util.h>
#include <log/log.h>
#include <utils/Trace.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/Fourcc.h>
#include <v4l2_codec2/common/H264NalParser.h>
#include <v4l2_codec2/common/HEVCNalParser.h>
#include <v4l2_codec2/common/LibyuvImageConverter.h>
#include <v4l2_codec2/v4l2/MdpImageConverter.h>
#include <v4l2_codec2/plugin_store/DmabufHelpers.h>

namespace android {
namespace {

constexpr size_t kNumExtraOutputBuffers = 4;

bool waitForDRC(const C2ConstLinearBlock& input, std::optional<VideoCodec> codec) {
    C2ReadView view = input.map().get();
    const uint8_t* pos = view.data();

    const uint8_t kVP9FrameTypeMask = 0x4;

    const uint8_t kVP8FrameTypeMask = 0x1;

    switch (*codec) {
    case VideoCodec::H264: {
        H264NalParser parser(view.data(), view.capacity());
        return parser.locateIDR();
    }
    case VideoCodec::HEVC: {
        HEVCNalParser parser(view.data(), view.capacity());
        return parser.locateIDR();
    }

    case VideoCodec::VP9:

        return ((pos[0] & kVP9FrameTypeMask) == 0);
    case VideoCodec::VP8:

        return ((pos[0] & kVP8FrameTypeMask) == 0);
    case VideoCodec::AV1:

        break;
    case VideoCodec::MPEG4:
    case VideoCodec::H263:
        break;
    }

    return false;
}

}

std::unique_ptr<VideoDecoder> V4L2Decoder::Create(
        uint32_t debugStreamId, const VideoCodec& codec, const size_t inputBufferSize,
        const size_t minNumOutputBuffers, GetPoolCB getPoolCb, OutputCB outputCb, ErrorCB errorCb,
        scoped_refptr<::base::SequencedTaskRunner> taskRunner, bool isSecure,
        uint32_t requestedPixelFormat) {
    std::unique_ptr<V4L2Decoder> decoder =
            ::base::WrapUnique<V4L2Decoder>(new V4L2Decoder(debugStreamId, taskRunner));
    if (!decoder->start(codec, inputBufferSize, minNumOutputBuffers, std::move(getPoolCb),
                        std::move(outputCb), std::move(errorCb), isSecure,
                        requestedPixelFormat)) {
        return nullptr;
    }
    return decoder;
}

V4L2Decoder::V4L2Decoder(uint32_t debugStreamId,
                         scoped_refptr<::base::SequencedTaskRunner> taskRunner)
      : mDebugStreamId(debugStreamId), mTaskRunner(std::move(taskRunner)) {
    ALOGV("%s()", __func__);

    mImageConverter = std::make_unique<LibyuvImageConverter>();
    mMdpImageConverter = MdpImageConverter::Create();
    if (mMdpImageConverter) {
        ALOGI("MDP hardware image converter ready");
    } else {
        ALOGI("MDP hardware image converter unavailable, using CPU libyuv detile");
    }
    mWeakThis = mWeakThisFactory.GetWeakPtr();
}

V4L2Decoder::~V4L2Decoder() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    mWeakThisFactory.InvalidateWeakPtrs();

    if (mOutputQueue) {
        mOutputQueue->streamoff();
        mOutputQueue->deallocateBuffers();
        mOutputQueue = nullptr;
    }
    if (mInputQueue) {
        mInputQueue->streamoff();
        mInputQueue->deallocateBuffers();
        mInputQueue = nullptr;
    }
    if (mDevice) {
        mDevice->stopPolling();
        mDevice = nullptr;
    }
    mInitialEosPending = false;
}

bool V4L2Decoder::start(const VideoCodec& codec, const size_t inputBufferSize,
                        const size_t minNumOutputBuffers, GetPoolCB getPoolCb, OutputCB outputCb,
                        ErrorCB errorCb, bool isSecure, uint32_t requestedPixelFormat) {
    ATRACE_CALL();
    ALOGV("%s(codec=%s, inputBufferSize=%zu, minNumOutputBuffers=%zu)", __func__,
          VideoCodecToString(codec), inputBufferSize, minNumOutputBuffers);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    mMinNumOutputBuffers = minNumOutputBuffers;
    mGetPoolCb = std::move(getPoolCb);
    mOutputCb = std::move(outputCb);
    mErrorCb = std::move(errorCb);
    mCodec = codec;
    mIsSecure = isSecure;
    mRequestedPixelFormat = requestedPixelFormat;

    if (mState == State::Error) {
        ALOGE("Ignore due to error state.");
        return false;
    }

    mDevice = V4L2Device::create(mDebugStreamId);

    const uint32_t inputPixelFormat = V4L2Device::videoCodecToPixFmt(codec);
    if (!mDevice->open(V4L2Device::Type::kDecoder, inputPixelFormat)) {
        ALOGE("Failed to open device for %s", VideoCodecToString(codec));
        return false;
    }

    if (!mDevice->hasCapabilities(V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING)) {
        ALOGE("Device does not have VIDEO_M2M_MPLANE and STREAMING capabilities.");
        return false;
    }

    if (!tryV4L2DecoderCmd(false)) {
        ALOGE("Device does not support flushing (V4L2_DEC_CMD_STOP)");
        return false;
    }

    struct v4l2_event_subscription sub;
    memset(&sub, 0, sizeof(sub));
    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    if (mDevice->ioctl(VIDIOC_SUBSCRIBE_EVENT, &sub) != 0) {
        ALOGE("ioctl() failed: VIDIOC_SUBSCRIBE_EVENT: V4L2_EVENT_SOURCE_CHANGE");
        return false;
    }

    mInputQueue = mDevice->getQueue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    mOutputQueue = mDevice->getQueue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    if (!mInputQueue || !mOutputQueue) {
        ALOGE("Failed to create V4L2 queue.");
        return false;
    }
    if (!setupInputFormat(inputPixelFormat, inputBufferSize)) {
        ALOGE("Failed to setup input format.");
        return false;
    }
    if (!setupInitialOutput()) {
        ALOGE("Unable to setup initial output");
        return false;
    }

    if (!mDevice->startPolling(mTaskRunner,
                               ::base::BindRepeating(&V4L2Decoder::serviceDeviceTask, mWeakThis),
                               ::base::BindRepeating(&V4L2Decoder::onError, mWeakThis))) {
        ALOGE("Failed to start polling V4L2 device.");
        return false;
    }

    setState(State::Idle);
    return true;
}

bool V4L2Decoder::setupInputFormat(const uint32_t inputPixelFormat, const size_t inputBufferSize) {
    ALOGV("%s(inputPixelFormat=%u, inputBufferSize=%zu)", __func__, inputPixelFormat,
          inputBufferSize);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    std::vector<uint32_t> formats =
            mDevice->enumerateSupportedPixelformats(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    if (std::find(formats.begin(), formats.end(), inputPixelFormat) == formats.end()) {
        ALOGE("Input codec s not supported by device.");
        return false;
    }

    auto format = mInputQueue->setFormat(inputPixelFormat, ui::Size(), inputBufferSize, 0);
    if (!format) {
        ALOGE("Failed to call IOCTL to set input format.");
        return false;
    }
    ALOG_ASSERT(format->fmt.pix_mp.pixelformat == inputPixelFormat);
    mInputBufferSize = format->fmt.pix_mp.plane_fmt[0].sizeimage;
    if (mInputBufferSize == 0) {
        return false;
    }

    if (mInputQueue->allocateBuffers(kNumInputBuffers, V4L2_MEMORY_DMABUF) == 0) {
        ALOGE("Failed to allocate input buffer.");
        return false;
    }
    if (!mInputQueue->streamon()) {
        ALOGE("Failed to streamon input queue.");
        return false;
    }
    return true;
}

bool V4L2Decoder::setupInitialOutput() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (!setupMinimalOutputFormat()) {
        ALOGE("Failed to set minimal resolution for initial output buffers");
        return false;
    }

    if (!startOutputQueue(1, V4L2_MEMORY_DMABUF)) {
        ALOGE("Failed to start initialy output queue");
        return false;
    }

    std::optional<V4L2WritableBufferRef> eosBuffer = mOutputQueue->getFreeBuffer();
    if (!eosBuffer) {
        ALOGE("Failed to acquire initial EOS buffer");
        return false;
    }

    TiledCaptureBuffer* tiledBuffer = mTiledCaptureBuffers.bufferAt(eosBuffer->bufferId());
    if (!tiledBuffer || !tiledBuffer->isValid()) {
        ALOGE("No internal tiled CAPTURE buffer for the initial EOS buffer");
        return false;
    }

    std::vector<int> fds = {tiledBuffer->fd()};
    if (!std::move(*eosBuffer).queueDMABuf(fds)) {
        ALOGE("Failed to queue initial EOS buffer");
        return false;
    }
    mInitialEosPending = true;

    return true;
}

bool V4L2Decoder::setupMinimalOutputFormat() {
    ui::Size minResolution, maxResolution;

    for (const uint32_t& pixfmt :
         mDevice->enumerateSupportedPixelformats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)) {
        if (std::find(kSupportedOutputFourccs.begin(), kSupportedOutputFourccs.end(), pixfmt) ==
            kSupportedOutputFourccs.end()) {
            ALOGD("Pixel format %s is not supported, skipping...", fourccToString(pixfmt).c_str());
            continue;
        }

        mDevice->getSupportedResolution(pixfmt, &minResolution, &maxResolution);
        if (minResolution.isEmpty()) {
            minResolution.set(128, 128);
        }

        if (mOutputQueue->setFormat(pixfmt, minResolution, 0) != std::nullopt) {
            return true;
        }
    }

    ALOGE("Failed to find supported pixel format");
    return false;
}

bool V4L2Decoder::startOutputQueue(size_t minOutputBuffersCount, enum v4l2_memory memory) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    const std::optional<struct v4l2_format> format = getFormatInfo();
    std::optional<size_t> numOutputBuffers = getNumOutputBuffers();
    if (!format || !numOutputBuffers) {
        return false;
    }
    *numOutputBuffers = std::max(*numOutputBuffers, minOutputBuffersCount);

    if (mOutputQueue->isStreaming()) {
        mOutputQueue->streamoff();
    }
    if (mOutputQueue->allocatedBuffersCount() > 0) {
        mOutputQueue->deallocateBuffers();
    }

    const ui::Size codedSize(format->fmt.pix_mp.width, format->fmt.pix_mp.height);
    if (!setupOutputFormat(codedSize, format->fmt.pix_mp.pixelformat)) {
        return false;
    }

    const std::optional<struct v4l2_format> adjustedFormat = getFormatInfo();
    if (!adjustedFormat) {
        return false;
    }
    mCodedSize.set(adjustedFormat->fmt.pix_mp.width, adjustedFormat->fmt.pix_mp.height);
    mVisibleRect = getVisibleRect(mCodedSize);

    ALOGI("Need %zu output buffers. coded size: %s, visible rect: %s", *numOutputBuffers,
          toString(mCodedSize).c_str(), toString(mVisibleRect).c_str());
    if (isEmpty(mCodedSize)) {
        ALOGE("Failed to get resolution from V4L2 driver.");
        return false;
    }

    mFrameAtDevice.clear();
    mBlockIdToV4L2Id.clear();
    while (!mReuseFrameQueue.empty()) {
        mReuseFrameQueue.pop();
    }

    const size_t adjustedNumOutputBuffers =
            mOutputQueue->allocateBuffers(*numOutputBuffers, memory);
    if (adjustedNumOutputBuffers == 0) {
        ALOGE("Failed to allocate output buffer.");
        return false;
    }

    ALOGV("Allocated %zu output buffers.", adjustedNumOutputBuffers);

    mCaptureStrideY = adjustedFormat->fmt.pix_mp.plane_fmt[0].bytesperline;
    if (mCaptureStrideY == 0) {
        mCaptureStrideY = static_cast<uint32_t>(mCodedSize.width);
    }
    mCaptureIsMt10 = adjustedFormat->fmt.pix_mp.pixelformat == kMt21S10TFourcc ||
                    adjustedFormat->fmt.pix_mp.pixelformat == kMt21S10RFourcc;
    mCaptureMt10Raster = adjustedFormat->fmt.pix_mp.pixelformat == kMt21S10RFourcc;
    const uint32_t captureBufferSize = adjustedFormat->fmt.pix_mp.plane_fmt[0].sizeimage;
    if (!mTiledCaptureBuffers.allocate(adjustedNumOutputBuffers, captureBufferSize)) {
        ALOGE("Failed to allocate %zu internal tiled CAPTURE buffers of %u bytes.",
              adjustedNumOutputBuffers, captureBufferSize);
        return false;
    }

    if (!mOutputQueue->streamon()) {
        ALOGE("Failed to streamon output queue.");
        return false;
    }

    return true;
}

void V4L2Decoder::decode(std::unique_ptr<ConstBitstreamBuffer> buffer, DecodeCB decodeCb) {
    ATRACE_CALL();
    ALOGV("%s(id=%d)", __func__, buffer->id);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState == State::Error) {
        ALOGE("Ignore due to error state.");
        mTaskRunner->PostTask(FROM_HERE, ::base::BindOnce(std::move(decodeCb),
                                                          VideoDecoder::DecodeStatus::kError));
        return;
    }

    if (mState == State::Idle) {
        setState(State::Decoding);
    }

    if (!mIsSecure && mInitialEosPending && !mPendingDRC)
        mPendingDRC = waitForDRC(buffer->dmabuf, mCodec);

    mDecodeRequests.push(DecodeRequest(std::move(buffer), std::move(decodeCb)));
    pumpDecodeRequest();
}

void V4L2Decoder::drain(DecodeCB drainCb) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    switch (mState) {
    case State::Idle:
        ALOGV("Nothing need to drain, ignore.");
        mTaskRunner->PostTask(
                FROM_HERE, ::base::BindOnce(std::move(drainCb), VideoDecoder::DecodeStatus::kOk));
        return;

    case State::Decoding:
        mDecodeRequests.push(DecodeRequest(nullptr, std::move(drainCb)));
        pumpDecodeRequest();
        return;

    case State::Draining:
    case State::Error:
        ALOGE("Ignore due to wrong state: %s", StateToString(mState));
        mTaskRunner->PostTask(FROM_HERE, ::base::BindOnce(std::move(drainCb),
                                                          VideoDecoder::DecodeStatus::kError));
        return;
    }
}

void V4L2Decoder::pumpDecodeRequest() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState != State::Decoding) return;

    while (!mDecodeRequests.empty()) {

        if (mDecodeRequests.front().buffer == nullptr) {
            ALOGV("Get drain request.");

            if (mInputQueue->queuedBuffersCount() > 0) {
                ALOGV("Wait for all input buffers dequeued.");
                return;
            }

            if (!mOutputQueue->isStreaming()) {
                ALOGV("Wait for output queue to start streaming");
                return;
            }

            auto request = std::move(mDecodeRequests.front());
            mDecodeRequests.pop();

            if (mInitialEosPending && !mPendingDRC) {
                ALOGV("Terminate drain, because there was no stream");
                mTaskRunner->PostTask(FROM_HERE, ::base::BindOnce(std::move(request.decodeCb),
                                                                  VideoDecoder::DecodeStatus::kOk));
                return;
            }

            if (!sendV4L2DecoderCmd(false)) {
                std::move(request.decodeCb).Run(VideoDecoder::DecodeStatus::kError);
                onError();
                return;
            }
            mDrainCb = std::move(request.decodeCb);
            setState(State::Draining);
            return;
        }

        const auto& bitstream = *mDecodeRequests.front().buffer;
        const auto* handle = bitstream.dmabuf.handle();
        struct stat allocation = {};
        if (!handle || handle->numFds < 1 || fstat(handle->data[0], &allocation) != 0 ||
            allocation.st_size <= 0 ||
            static_cast<uint64_t>(allocation.st_size) > std::numeric_limits<size_t>::max()) {
            ALOGE("Invalid input allocation");
            onError();
            return;
        }
        const size_t allocationSize = static_cast<size_t>(allocation.st_size);
        auto dma_buf_id = getDmabufId(handle->data[0]);
        if (!dma_buf_id) {
            ALOGE("Failed to get dmabuf id");
            onError();
            return;
        }

        std::optional<V4L2WritableBufferRef> inputBuffer;
        size_t targetIndex = 0;
        if (allocationSize < mInputBufferSize || bitstream.offset != 0) {
            for (size_t index = 0; index < kNumInputBuffers; ++index) {
                if (mInputStaging[index].isValid()) {
                    inputBuffer = mInputQueue->getFreeBuffer(index);
                    if (inputBuffer) {
                        break;
                    }
                }
            }
        }

        for (; targetIndex < mNextInputBufferId; targetIndex++) {
            if (mLastDmaBufferId[targetIndex] == dma_buf_id) {
                break;
            }
        }

        if (!inputBuffer && targetIndex < kNumInputBuffers) {

            if (targetIndex == mNextInputBufferId) {
                mNextInputBufferId++;
            }

            inputBuffer = mInputQueue->getFreeBuffer(targetIndex);
        }

        if (!inputBuffer) {
            inputBuffer = mInputQueue->getFreeBuffer();
        }

        if (!inputBuffer) {
            ALOGV("There is no free input buffer.");
            return;
        }

        mLastDmaBufferId[inputBuffer->bufferId()] = *dma_buf_id;

        const int32_t bitstreamId = bitstream.id;
        if (!queueInputBuffer(std::move(*inputBuffer), bitstream, allocationSize)) {
            ALOGE("Failed to prepare or queue input buffer, bitstreamId=%d", bitstreamId);
            onError();
            return;
        }
        auto request = std::move(mDecodeRequests.front());
        mDecodeRequests.pop();

        mPendingDecodeCbs.insert(std::make_pair(bitstreamId, std::move(request.decodeCb)));
    }
}

bool V4L2Decoder::queueInputBuffer(V4L2WritableBufferRef inputBuffer,
                                   const ConstBitstreamBuffer& buffer, size_t allocationSize) {
    const size_t planeSize = inputBuffer.getPlaneSize(0);
    const size_t blockOffset = buffer.dmabuf.offset();
    const size_t blockSize = buffer.dmabuf.size();
    if (buffer.size == 0 || buffer.size > planeSize ||
        buffer.offset > allocationSize || buffer.size > allocationSize - buffer.offset ||
        buffer.offset < blockOffset || buffer.offset - blockOffset > blockSize ||
        buffer.size > blockSize - (buffer.offset - blockOffset)) {
        return false;
    }
    size_t offset = buffer.offset;
    int fd = buffer.dmabuf.handle()->data[0];
    if (allocationSize < planeSize || offset != 0) {
        if (mIsSecure || inputBuffer.bufferId() >= kNumInputBuffers) {
            return false;
        }
        C2ReadView view = buffer.dmabuf.map().get();
        const size_t viewOffset = offset - blockOffset;
        if (view.error() != C2_OK || !view.data() || viewOffset > view.capacity() ||
            buffer.size > view.capacity() - viewOffset) {
            return false;
        }
        auto& staging = mInputStaging[inputBuffer.bufferId()];
        if (!staging.copyFrom(view.data() + viewOffset, buffer.size, planeSize)) {
            return false;
        }
        fd = staging.fd();
        offset = 0;
    }
    inputBuffer.setTimeStamp({.tv_sec = buffer.id});
    inputBuffer.setPlaneDataOffset(0, offset);
    inputBuffer.setPlaneBytesUsed(0, offset + buffer.size);
    return std::move(inputBuffer).queueDMABuf({fd});
}

void V4L2Decoder::flush() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState == State::Idle) {
        ALOGV("Nothing need to flush, ignore.");
        return;
    }
    if (mState == State::Error) {
        ALOGE("Ignore due to error state.");
        return;
    }

    for (auto& item : mPendingDecodeCbs) {
        std::move(item.second).Run(VideoDecoder::DecodeStatus::kAborted);
    }
    mPendingDecodeCbs.clear();
    if (mDrainCb) {
        std::move(mDrainCb).Run(VideoDecoder::DecodeStatus::kAborted);
    }

    while (!mDecodeRequests.empty()) {
        auto request = std::move(mDecodeRequests.front());
        mDecodeRequests.pop();
        std::move(request.decodeCb).Run(VideoDecoder::DecodeStatus::kAborted);
    }

    const bool isOutputStreaming = mOutputQueue->isStreaming();
    mDevice->stopPolling();
    mOutputQueue->streamoff();

    for (auto& [v4l2Id, frame] : mFrameAtDevice) {

        auto blockIdIter =
                std::find_if(mBlockIdToV4L2Id.begin(), mBlockIdToV4L2Id.end(),
                             [v4l2Id = v4l2Id](const auto& el) { return el.second == v4l2Id; });

        ALOG_ASSERT(blockIdIter != mBlockIdToV4L2Id.end());
        size_t blockId = blockIdIter->first;
        mReuseFrameQueue.push(std::make_pair(blockId, std::move(frame)));
    }
    mFrameAtDevice.clear();

    mInputQueue->streamoff();

    mInputQueue->streamon();
    if (isOutputStreaming) {
        mOutputQueue->streamon();
    }

    if (mVideoFramePool) {
        tryFetchVideoFrame();
    }

    if (!mDevice->startPolling(mTaskRunner,
                               ::base::BindRepeating(&V4L2Decoder::serviceDeviceTask, mWeakThis),
                               ::base::BindRepeating(&V4L2Decoder::onError, mWeakThis))) {
        ALOGE("Failed to start polling V4L2 device.");
        onError();
        return;
    }

    setState(State::Idle);
}

void V4L2Decoder::serviceDeviceTask(bool event) {
    ATRACE_CALL();
    ALOGV("%s(event=%d) state=%s InputQueue(%s):%zu+%zu/%zu, OutputQueue(%s):%zu+%zu/%zu", __func__,
          event, StateToString(mState), (mInputQueue->isStreaming() ? "streamon" : "streamoff"),
          mInputQueue->freeBuffersCount(), mInputQueue->queuedBuffersCount(),
          mInputQueue->allocatedBuffersCount(),
          (mOutputQueue->isStreaming() ? "streamon" : "streamoff"),
          mOutputQueue->freeBuffersCount(), mOutputQueue->queuedBuffersCount(),
          mOutputQueue->allocatedBuffersCount());

    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState == State::Error) return;

    bool inputDequeued = false;
    while (mInputQueue->queuedBuffersCount() > 0) {
        bool success;
        V4L2ReadableBufferRef dequeuedBuffer;
        std::tie(success, dequeuedBuffer) = mInputQueue->dequeueBuffer();
        if (!success) {
            ALOGE("Failed to dequeue buffer from input queue.");
            onError();
            return;
        }
        if (!dequeuedBuffer) break;

        inputDequeued = true;

        int32_t id = dequeuedBuffer->getTimeStamp().tv_sec;
        ALOGV("DQBUF from input queue, bitstreamId=%d", id);
        auto it = mPendingDecodeCbs.find(id);
        if (it == mPendingDecodeCbs.end()) {
            ALOGW("Callback is already abandoned.");
            continue;
        }
        std::move(it->second).Run(VideoDecoder::DecodeStatus::kOk);
        mPendingDecodeCbs.erase(it);
    }

    bool outputDequeued = false;
    while (mOutputQueue->queuedBuffersCount() > 0) {
        bool success;
        V4L2ReadableBufferRef dequeuedBuffer;
        std::tie(success, dequeuedBuffer) = mOutputQueue->dequeueBuffer();
        if (!success) {
            ALOGE("Failed to dequeue buffer from output queue.");
            onError();
            return;
        }
        if (!dequeuedBuffer) break;

        outputDequeued = true;

        const size_t bufferId = dequeuedBuffer->bufferId();
        const int32_t bitstreamId = static_cast<int32_t>(dequeuedBuffer->getTimeStamp().tv_sec);
        const size_t bytesUsed = dequeuedBuffer->getPlaneBytesUsed(0);
        const bool isLast = dequeuedBuffer->isLast();
        ALOGV("DQBUF from output queue, bufferId=%zu, bitstreamId=%d, bytesused=%zu, isLast=%d",
              bufferId, bitstreamId, bytesUsed, isLast);

        auto it = mFrameAtDevice.find(bufferId);
        if (it == mFrameAtDevice.end() && isLast) continue;
        ALOG_ASSERT(it != mFrameAtDevice.end(), "buffer %zu is not found at mFrameAtDevice",
                    bufferId);
        auto frame = std::move(it->second);
        mFrameAtDevice.erase(it);

        if (bytesUsed > 0) {
            TiledCaptureBuffer* decodedTiled = mTiledCaptureBuffers.bufferAt(bufferId);
            if (!decodedTiled || !decodedTiled->isValid()) {
                ALOGE("No internal tiled CAPTURE buffer for bufferId=%zu", bufferId);
                onError();
                return;
            }

            bool detiled;
            bool primaryFailed = false;
            if (mCaptureIsMt10) {
                decodedTiled->syncStartRead();
                TiledMT10Image tiledImage;
                tiledImage.data = decodedTiled->data();
                tiledImage.strideY = mCaptureStrideY;
                tiledImage.raster = mCaptureMt10Raster;
                tiledImage.dmaBufFd = decodedTiled->fd();
                tiledImage.dmaBufSize = decodedTiled->size();
                detiled = mOutputIsDownconvertedTo8Bit
                                  ? frame->detileFrom10To8(mImageConverter.get(), tiledImage,
                                                           mCodedSize.width, mCodedSize.height)
                                  : frame->detileFrom10(mImageConverter.get(), tiledImage,
                                                        mCodedSize.width, mCodedSize.height);
                decodedTiled->syncEndRead();
            } else {
                TiledMM21Image tiledImage;
                tiledImage.y = decodedTiled->data();
                tiledImage.uv = decodedTiled->data() +
                                static_cast<size_t>(mCaptureStrideY) * mCodedSize.height;
                tiledImage.strideY = mCaptureStrideY;
                tiledImage.strideUV = mCaptureStrideY;
                tiledImage.dmaBufFd = decodedTiled->fd();
                tiledImage.dmaBufSize = decodedTiled->size();
                ImageConverter* const primaryConverter =
                        (mMdpImageConverter && !mMdpFailed) ? mMdpImageConverter.get() : nullptr;
                detiled = primaryConverter &&
                          frame->detileFrom(primaryConverter, nullptr, tiledImage,
                                            mCodedSize.width, mCodedSize.height, &primaryFailed);
                if (!detiled) {
                    decodedTiled->syncStartRead();
                    detiled = frame->detileFrom(nullptr, mImageConverter.get(), tiledImage,
                                                mCodedSize.width, mCodedSize.height, nullptr);
                    decodedTiled->syncEndRead();
                }
            }

            if (primaryFailed && !mMdpFailed) {
                mMdpFailed = true;
                ALOGE("MDP hardware image converter failed, falling back to CPU libyuv detile "
                      "for the rest of this stream");
            }

            if (!detiled) {
                ALOGE("Failed to detile CAPTURE bufferId=%zu into output frame", bufferId);
                onError();
                return;
            }

            ALOGV("Send output frame(bitstreamId=%d) to client", bitstreamId);
            frame->setBitstreamId(bitstreamId);
            frame->setVisibleRect(mVisibleRect);
            mOutputCb.Run(std::move(frame));
        } else {

            ALOGV("Recycle empty buffer %zu back to V4L2 output queue.", bufferId);
            dequeuedBuffer.reset();
            auto outputBuffer = mOutputQueue->getFreeBuffer(bufferId);
            ALOG_ASSERT(outputBuffer, "V4L2 output queue slot %zu is not freed.", bufferId);

            TiledCaptureBuffer* recycledTiled = mTiledCaptureBuffers.bufferAt(bufferId);
            if (!recycledTiled || !recycledTiled->isValid()) {
                ALOGE("%s(): No internal tiled CAPTURE buffer for bufferId=%zu.", __func__,
                      bufferId);
                onError();
                return;
            }
            std::vector<int> recycledFds = {recycledTiled->fd()};
            if (!std::move(*outputBuffer).queueDMABuf(recycledFds)) {
                ALOGE("%s(): Failed to recycle empty buffer to output queue.", __func__);
                onError();
                return;
            }
            mFrameAtDevice.insert(std::make_pair(bufferId, std::move(frame)));
        }

        if (mDrainCb && isLast) {
            ALOGV("All buffers are drained.");
            sendV4L2DecoderCmd(true);
            std::move(mDrainCb).Run(VideoDecoder::DecodeStatus::kOk);
            setState(State::Idle);
        }
    }

    if (event && dequeueResolutionChangeEvent()) {
        if (!changeResolution()) {
            onError();
            return;
        }
    }

    if (inputDequeued) {
        mTaskRunner->PostTask(FROM_HERE,
                              ::base::BindOnce(&V4L2Decoder::pumpDecodeRequest, mWeakThis));
    }

    if (outputDequeued) {
        mTaskRunner->PostTask(FROM_HERE,
                              ::base::BindOnce(&V4L2Decoder::tryFetchVideoFrame, mWeakThis));
    }
}

bool V4L2Decoder::dequeueResolutionChangeEvent() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_event ev;
    memset(&ev, 0, sizeof(ev));
    while (mDevice->ioctl(VIDIOC_DQEVENT, &ev) == 0) {
        if (ev.type == V4L2_EVENT_SOURCE_CHANGE &&
            ev.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) {
            return true;
        }
    }
    return false;
}

bool V4L2Decoder::changeResolution() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    mInitialEosPending = false;

    if (!startOutputQueue(mMinNumOutputBuffers, V4L2_MEMORY_DMABUF)) {
        ALOGE("Failed to start output queue during DRC.");
        return false;
    }

    if (!mDecodeRequests.empty() && mDecodeRequests.front().buffer == nullptr) {
        mTaskRunner->PostTask(FROM_HERE,
                              ::base::BindOnce(&V4L2Decoder::pumpDecodeRequest, mWeakThis));
    }

    mVideoFramePool.reset();

    const std::optional<struct v4l2_format> format = getFormatInfo();
    if (!format) {
        ALOGE("Failed to query capture format during resolution change.");
        return false;
    }
    const uint32_t capturePixfmt = format->fmt.pix_mp.pixelformat;
    const bool captureIs10Bit = capturePixfmt == V4L2_PIX_FMT_P010 ||
                                capturePixfmt == kMt21S10TFourcc ||
                                capturePixfmt == kMt21S10RFourcc;
    mOutputIsDownconvertedTo8Bit =
            captureIs10Bit &&
            mRequestedPixelFormat == static_cast<uint32_t>(HalPixelFormat::YCBCR_420_888);
    HalPixelFormat outputFormat = (captureIs10Bit && !mOutputIsDownconvertedTo8Bit)
                                          ? HalPixelFormat::YCBCR_P010
                                          : HalPixelFormat::YCBCR_420_888;
    mVideoFramePool = mGetPoolCb.Run(mCodedSize, outputFormat,
                                     mOutputQueue->allocatedBuffersCount());
    if (!mVideoFramePool) {
        ALOGE("Failed to get block pool with size: %s", toString(mCodedSize).c_str());
        return false;
    }

    tryFetchVideoFrame();
    return true;
}

bool V4L2Decoder::setupOutputFormat(const ui::Size& size, const uint32_t requestedpixfmt) {
    if (requestedpixfmt != 0) {
        if (std::find(kSupportedOutputFourccs.begin(), kSupportedOutputFourccs.end(), requestedpixfmt) !=
            kSupportedOutputFourccs.end()) {
            if (mOutputQueue->setFormat(requestedpixfmt, size, 0) != std::nullopt) {
                return true;
            }
        }
        ALOGD("Decoder requested pixel format %s is not supported, skipping...", fourccToString(requestedpixfmt).c_str());
    }

    for (const uint32_t& pixfmt :
         mDevice->enumerateSupportedPixelformats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)) {
        if (std::find(kSupportedOutputFourccs.begin(), kSupportedOutputFourccs.end(), pixfmt) ==
            kSupportedOutputFourccs.end()) {
            ALOGD("Pixel format %s is not supported, skipping...", fourccToString(pixfmt).c_str());
            continue;
        }

        if (mOutputQueue->setFormat(pixfmt, size, 0) != std::nullopt) {
            return true;
        }
    }

    ALOGE("Failed to find supported pixel format");
    return false;
}

void V4L2Decoder::tryFetchVideoFrame() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (!mVideoFramePool) {
        ALOGE("mVideoFramePool is null, failed to get the instance after resolution change?");
        onError();
        return;
    }

    if (mOutputQueue->freeBuffersCount() == 0) {
        ALOGV("No free V4L2 output buffers, ignore.");
        return;
    }

    if (mReuseFrameQueue.empty()) {
        if (!mVideoFramePool->getVideoFrame(
                    ::base::BindOnce(&V4L2Decoder::onVideoFrameReady, mWeakThis))) {
            ALOGV("%s(): Previous callback is running, ignore.", __func__);
        }

        return;
    }

    uint32_t blockId = mReuseFrameQueue.front().first;
    std::unique_ptr<VideoFrame> frame = std::move(mReuseFrameQueue.front().second);
    mReuseFrameQueue.pop();

    mTaskRunner->PostTask(FROM_HERE, ::base::BindOnce(&V4L2Decoder::onVideoFrameReady, mWeakThis,
                                                      std::make_pair(std::move(frame), blockId)));
}

void V4L2Decoder::onVideoFrameReady(
        std::optional<VideoFramePool::FrameWithBlockId> frameWithBlockId) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (!frameWithBlockId) {
        ALOGE("Got nullptr VideoFrame.");
        onError();
        return;
    }

    std::unique_ptr<VideoFrame> frame;
    uint32_t blockId;
    std::tie(frame, blockId) = std::move(*frameWithBlockId);

    std::optional<V4L2WritableBufferRef> outputBuffer;

    auto iter = mBlockIdToV4L2Id.find(blockId);
    if (iter != mBlockIdToV4L2Id.end()) {

        outputBuffer = mOutputQueue->getFreeBuffer(iter->second);
        if (!outputBuffer) {

            ALOGW("%s(): The frame have been supplied again, despite being already enqueued",
                  __func__);
            tryFetchVideoFrame();
            return;
        }
    } else if (mBlockIdToV4L2Id.size() < mOutputQueue->allocatedBuffersCount()) {

        const size_t v4l2BufferId = mBlockIdToV4L2Id.size();
        mBlockIdToV4L2Id.emplace(blockId, v4l2BufferId);
        outputBuffer = mOutputQueue->getFreeBuffer(v4l2BufferId);
        ALOG_ASSERT(v4l2BufferId == outputBuffer->bufferId());
    } else {

        ALOGE("Got more different blocks than we have V4L2 buffers for.");
    }

    if (!outputBuffer) {
        ALOGE("V4L2 buffer not available. blockId=%u", blockId);
        onError();
        return;
    }

    uint32_t v4l2Id = outputBuffer->bufferId();
    ALOGV("QBUF to output queue, blockId=%u, V4L2Id=%u", blockId, v4l2Id);

    TiledCaptureBuffer* tiledBuffer = mTiledCaptureBuffers.bufferAt(v4l2Id);
    if (!tiledBuffer || !tiledBuffer->isValid()) {
        ALOGE("%s(): No internal tiled CAPTURE buffer for V4L2Id=%u", __func__, v4l2Id);
        onError();
        return;
    }
    std::vector<int> tiledFds = {tiledBuffer->fd()};
    if (!std::move(*outputBuffer).queueDMABuf(tiledFds)) {
        ALOGE("%s(): Failed to QBUF to output queue, blockId=%u, V4L2Id=%u", __func__, blockId,
              v4l2Id);
        onError();
        return;
    }
    if (mFrameAtDevice.find(v4l2Id) != mFrameAtDevice.end()) {
        ALOGE("%s(): V4L2 buffer %d already enqueued.", __func__, v4l2Id);
        onError();
        return;
    }
    mFrameAtDevice.insert(std::make_pair(v4l2Id, std::move(frame)));

    tryFetchVideoFrame();
}

std::optional<size_t> V4L2Decoder::getNumOutputBuffers() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    if (mDevice->ioctl(VIDIOC_G_CTRL, &ctrl) != 0) {
        ALOGE("ioctl() failed: VIDIOC_G_CTRL");
        return std::nullopt;
    }
    ALOGV("%s() V4L2_CID_MIN_BUFFERS_FOR_CAPTURE returns %u", __func__, ctrl.value);

    return ctrl.value + kNumExtraOutputBuffers;
}

std::optional<struct v4l2_format> V4L2Decoder::getFormatInfo() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (mDevice->ioctl(VIDIOC_G_FMT, &format) != 0) {
        ALOGE("ioctl() failed: VIDIOC_G_FMT");
        return std::nullopt;
    }

    return format;
}

Rect V4L2Decoder::getVisibleRect(const ui::Size& codedSize) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_rect visible_rect = {};
    struct v4l2_selection selection_arg;
    memset(&selection_arg, 0, sizeof(selection_arg));
    selection_arg.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    selection_arg.target = V4L2_SEL_TGT_COMPOSE;

    if (mDevice->ioctl(VIDIOC_G_SELECTION, &selection_arg) == 0) {
        ALOGV("VIDIOC_G_SELECTION is supported");
        visible_rect = selection_arg.r;
    } else {
        ALOGV("Fallback to VIDIOC_G_CROP");
        struct v4l2_crop crop_arg;
        memset(&crop_arg, 0, sizeof(crop_arg));
        crop_arg.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (mDevice->ioctl(VIDIOC_G_CROP, &crop_arg) != 0) {
            ALOGW("ioctl() VIDIOC_G_CROP failed");
            return Rect(codedSize.width, codedSize.height);
        }
        visible_rect = crop_arg.c;
    }

    Rect rect(visible_rect.left, visible_rect.top, visible_rect.left + visible_rect.width,
              visible_rect.top + visible_rect.height);
    ALOGV("visible rectangle is %s", toString(rect).c_str());
    if (!contains(Rect(codedSize.width, codedSize.height), rect)) {
        ALOGW("visible rectangle %s is not inside coded size %s", toString(rect).c_str(),
              toString(codedSize).c_str());
        return Rect(codedSize.width, codedSize.height);
    }
    if (rect.isEmpty()) {
        ALOGW("visible size is empty");
        return Rect(codedSize.width, codedSize.height);
    }

    return rect;
}

bool V4L2Decoder::sendV4L2DecoderCmd(bool start) {
    ALOGV("%s(start=%d)", __func__, start);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_decoder_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = start ? V4L2_DEC_CMD_START : V4L2_DEC_CMD_STOP;
    if (mDevice->ioctl(VIDIOC_DECODER_CMD, &cmd) != 0) {
        ALOGE("ioctl() VIDIOC_DECODER_CMD failed: start=%d", start);
        return false;
    }

    return true;
}

bool V4L2Decoder::tryV4L2DecoderCmd(bool start) {
    ALOGV("%s(start=%d)", __func__, start);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_decoder_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = start ? V4L2_DEC_CMD_START : V4L2_DEC_CMD_STOP;
    if (mDevice->ioctl(VIDIOC_TRY_DECODER_CMD, &cmd) != 0) {
        return false;
    }

    return true;
}

void V4L2Decoder::onError() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    setState(State::Error);
    mErrorCb.Run();
}

void V4L2Decoder::setState(State newState) {
    ALOGV("%s(%s)", __func__, StateToString(newState));
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState == newState) return;
    if (mState == State::Error) {
        ALOGV("Already in Error state.");
        return;
    }

    switch (newState) {
    case State::Idle:
        break;
    case State::Decoding:
        break;
    case State::Draining:
        if (mState != State::Decoding) newState = State::Error;
        break;
    case State::Error:
        break;
    }

    ALOGI("Set state %s => %s", StateToString(mState), StateToString(newState));
    mState = newState;
}

const char* V4L2Decoder::StateToString(State state) {
    switch (state) {
    case State::Idle:
        return "Idle";
    case State::Decoding:
        return "Decoding";
    case State::Draining:
        return "Draining";
    case State::Error:
        return "Error";
    }
}

}
