// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define ATRACE_TAG ATRACE_TAG_VIDEO
#define LOG_TAG "EncodeComponent"

#include <v4l2_codec2/components/EncodeComponent.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include <C2AllocatorGralloc.h>
#include <C2BlockInternal.h>
#include <C2PlatformSupport.h>
#include <C2Work.h>
#include <android/hardware/graphics/common/1.0/types.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <log/log.h>
#include <media/stagefright/MediaDefs.h>
#include <ui/GraphicBuffer.h>
#include <ui/Size.h>
#include <utils/Trace.h>
#include <vndk/hardware_buffer.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/EncodeHelpers.h>
#include <v4l2_codec2/common/FormatConverter.h>
#include <v4l2_codec2/common/H264.h>
#include <v4l2_codec2/components/BitstreamBuffer.h>
#include <v4l2_codec2/components/EncodeInterface.h>
#include <v4l2_codec2/components/VideoEncoder.h>

using android::hardware::graphics::common::V1_0::BufferUsage;

namespace android {

namespace {

bool isDmaBufFd(int fd) {
    char path[32];
    char link[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(path, link, sizeof(link));
    return n >= 7 && strncmp(link, "/dmabuf", 7) == 0;
}

std::vector<int> getInputBlockFds(const C2ConstGraphicBlock& block) {
    std::vector<int> fds;
    std::shared_ptr<_C2BlockPoolData> poolData = _C2BlockFactory::GetGraphicBlockPoolData(block);
    const native_handle_t* handle = nullptr;
    if (poolData && poolData->getType() == _C2BlockPoolData::TYPE_AHWBUFFER) {
        AHardwareBuffer* ahwb = nullptr;
        if (!_C2BlockFactory::GetAHardwareBuffer(poolData, &ahwb) || !ahwb) {
            ALOGE("Failed to get the AHardwareBuffer of an AHWBUFFER-backed input block");
            return fds;
        }
        handle = AHardwareBuffer_getNativeHandle(ahwb);
    } else {
        handle = block.handle();
    }
    if (!handle) {
        ALOGE("Input block has no native handle");
        return fds;
    }
    for (int i = 0; i < handle->numFds; i++) {
        if (isDmaBufFd(handle->data[i])) {
            fds.emplace_back(handle->data[i]);
        }
    }
    if (fds.empty()) {
        ALOGE("Input block has no dma-buf among its %d fds", handle->numFds);
    }
    return fds;
}

std::unique_ptr<VideoEncoder::InputFrame> createInputFrame(
        const C2ConstGraphicBlock& block, VideoPixelFormat format,
        const std::vector<VideoFramePlane>& planes, uint64_t index, int64_t timestamp) {
    std::vector<int> fds = getInputBlockFds(block);
    if (fds.empty()) {
        return nullptr;
    }

    return std::make_unique<VideoEncoder::InputFrame>(std::move(fds), planes, format, index,
                                                      timestamp);
}
}

std::optional<std::vector<VideoFramePlane>> getVideoFrameLayout(const C2ConstGraphicBlock& block,
                                                                VideoPixelFormat* format) {
    ALOGV("%s()", __func__);

    C2PlanarLayout layout = block.map().get().layout();

    if (layout.type == C2PlanarLayout::TYPE_UNKNOWN) {
        std::unique_ptr<ImplDefinedToRGBXMap> idMap = ImplDefinedToRGBXMap::create(block);
        if (idMap == nullptr) {
            ALOGE("Unable to parse RGBX_8888 from IMPLEMENTATION_DEFINED");
            return std::nullopt;
        }
        layout.type = C2PlanarLayout::TYPE_RGB;

        layout.numPlanes = 3;
        layout.rootPlanes = 1;
        layout.planes[C2PlanarLayout::PLANE_R].offset = idMap->offset();
        layout.planes[C2PlanarLayout::PLANE_R].rowInc = idMap->rowInc();
    }

    std::vector<uint32_t> offsets(layout.numPlanes, 0u);
    std::vector<uint32_t> strides(layout.numPlanes, 0u);
    switch (layout.type) {
    case C2PlanarLayout::TYPE_YUV: {
        android_ycbcr ycbcr = getGraphicBlockInfo(block);
        offsets[C2PlanarLayout::PLANE_Y] =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ycbcr.y));
        offsets[C2PlanarLayout::PLANE_U] =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ycbcr.cb));
        offsets[C2PlanarLayout::PLANE_V] =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ycbcr.cr));
        strides[C2PlanarLayout::PLANE_Y] = static_cast<uint32_t>(ycbcr.ystride);
        strides[C2PlanarLayout::PLANE_U] = static_cast<uint32_t>(ycbcr.cstride);
        strides[C2PlanarLayout::PLANE_V] = static_cast<uint32_t>(ycbcr.cstride);

        bool crcb = false;
        if (offsets[C2PlanarLayout::PLANE_U] > offsets[C2PlanarLayout::PLANE_V]) {

            std::swap(offsets[C2PlanarLayout::PLANE_U], offsets[C2PlanarLayout::PLANE_V]);
            crcb = true;
        }

        bool semiplanar = false;
        if (ycbcr.chroma_step >
            offsets[C2PlanarLayout::PLANE_V] - offsets[C2PlanarLayout::PLANE_U]) {
            semiplanar = true;
        }

        if (!crcb && !semiplanar) {
            *format = VideoPixelFormat::I420;
        } else if (!crcb && semiplanar) {
            *format = VideoPixelFormat::NV12;
        } else if (crcb && !semiplanar) {
            *format = VideoPixelFormat::YV12;
        } else {
            *format = VideoPixelFormat::NV21;
        }
        break;
    }
    case C2PlanarLayout::TYPE_RGB: {
        offsets[C2PlanarLayout::PLANE_R] = layout.planes[C2PlanarLayout::PLANE_R].offset;
        strides[C2PlanarLayout::PLANE_R] =
                static_cast<uint32_t>(layout.planes[C2PlanarLayout::PLANE_R].rowInc);
        *format = VideoPixelFormat::ARGB;
        break;
    }
    default:
        ALOGW("Unknown layout type: %u", static_cast<uint32_t>(layout.type));
        return std::nullopt;
    }

    std::vector<VideoFramePlane> planes;
    for (uint32_t i = 0; i < layout.rootPlanes; ++i) {

        planes.push_back({strides[i], offsets[i], 0});
    }
    return planes;
}

std::optional<uint32_t> getVideoFrameStride(VideoPixelFormat format, ui::Size size) {

    std::shared_ptr<C2BlockPool> pool;
    c2_status_t status = GetCodec2BlockPool(C2BlockPool::BASIC_GRAPHIC, nullptr, &pool);
    if (status != C2_OK) {
        ALOGE("Failed to get basic graphic block pool (err=%d)", status);
        return std::nullopt;
    }

    HalPixelFormat halFormat = (format == VideoPixelFormat::I420) ? HalPixelFormat::YV12
                                                                  : HalPixelFormat::YCBCR_420_888;

    std::shared_ptr<C2GraphicBlock> block;
    status = pool->fetchGraphicBlock(size.width, size.height, static_cast<uint32_t>(halFormat),
                                     C2MemoryUsage(C2MemoryUsage::CPU_READ), &block);
    if (status != C2_OK) {
        ALOGE("Failed to fetch graphic block (err=%d)", status);
        return std::nullopt;
    }

    const C2ConstGraphicBlock constBlock = block->share(C2Rect(size.width, size.height), C2Fence());
    VideoPixelFormat pixelFormat;
    std::optional<std::vector<VideoFramePlane>> planes =
            getVideoFrameLayout(constBlock, &pixelFormat);
    if (!planes || planes.value().empty()) {
        ALOGE("Failed to get video frame layout from block");
        return std::nullopt;
    }

    return planes.value()[0].mStride;
}

EncodeComponent::EncodeComponent(C2String name, c2_node_id_t id,
                                 std::shared_ptr<EncodeInterface> interface)
      : mName(name),
        mId(id),
        mInterface(std::move(interface)),
        mComponentState(ComponentState::LOADED) {
    ALOGV("%s(%s)", __func__, name.c_str());
}

EncodeComponent::~EncodeComponent() {
    ALOGV("%s()", __func__);

    if (mEncoderThread.IsRunning() && !mEncoderTaskRunner->RunsTasksInCurrentSequence()) {
        mEncoderTaskRunner->PostTask(
                FROM_HERE, ::base::BindOnce(
                                   [](::base::WeakPtrFactory<EncodeComponent>* weakPtrFactory,
                                      std::unique_ptr<VideoEncoder>* encoder) {
                                       weakPtrFactory->InvalidateWeakPtrs();
                                       encoder->reset();
                                   },
                                   &mWeakThisFactory, &mEncoder));
        mEncoderThread.Stop();
    }

    ALOGV("%s(): done", __func__);
}

c2_status_t EncodeComponent::start() {
    ALOGV("%s()", __func__);

    std::lock_guard<std::mutex> lock(mComponentLock);

    if (mComponentState != ComponentState::LOADED) {
        return C2_BAD_STATE;
    }

    if (!mEncoderThread.Start()) {
        ALOGE("Failed to start encoder thread");
        return C2_CORRUPTED;
    }
    mEncoderTaskRunner = mEncoderThread.task_runner();
    mWeakThis = mWeakThisFactory.GetWeakPtr();

    ::base::WaitableEvent done;
    bool success = false;
    mEncoderTaskRunner->PostTask(
            FROM_HERE, ::base::Bind(&EncodeComponent::startTask, mWeakThis, &success, &done));
    done.Wait();

    if (!success) {
        ALOGE("Failed to initialize encoder");
        return C2_CORRUPTED;
    }

    setComponentState(ComponentState::RUNNING);
    return C2_OK;
}

c2_status_t EncodeComponent::stop() {
    ALOGV("%s()", __func__);

    std::lock_guard<std::mutex> lock(mComponentLock);

    if (mComponentState != ComponentState::RUNNING && mComponentState != ComponentState::ERROR) {
        return C2_BAD_STATE;
    }

    if (!mEncoderThread.IsRunning()) {
        return C2_OK;
    }

    ::base::WaitableEvent done;
    mEncoderTaskRunner->PostTask(FROM_HERE,
                                 ::base::BindOnce(&EncodeComponent::stopTask, mWeakThis, &done));
    done.Wait();
    mEncoderThread.Stop();

    setComponentState(ComponentState::LOADED);

    ALOGV("%s() - done", __func__);
    return C2_OK;
}

c2_status_t EncodeComponent::reset() {
    ALOGV("%s()", __func__);

    if (mComponentState == ComponentState::UNLOADED) {
        return C2_BAD_STATE;
    }

    stop();

    return C2_OK;
}

c2_status_t EncodeComponent::release() {
    ALOGV("%s()", __func__);

    reset();

    setComponentState(ComponentState::UNLOADED);
    return C2_OK;
}

c2_status_t EncodeComponent::queue_nb(std::list<std::unique_ptr<C2Work>>* const items) {
    ALOGV("%s()", __func__);

    if (mComponentState != ComponentState::RUNNING) {
        ALOGE("Trying to queue work item while component is not running");
        return C2_BAD_STATE;
    }

    while (!items->empty()) {
        mEncoderTaskRunner->PostTask(FROM_HERE,
                                     ::base::BindOnce(&EncodeComponent::queueTask, mWeakThis,
                                                      std::move(items->front())));
        items->pop_front();
    }

    return C2_OK;
}

c2_status_t EncodeComponent::drain_nb(drain_mode_t mode) {
    ALOGV("%s()", __func__);

    if (mode == DRAIN_CHAIN) {
        return C2_OMITTED;
    }

    if (mComponentState != ComponentState::RUNNING) {
        return C2_BAD_STATE;
    }

    mEncoderTaskRunner->PostTask(FROM_HERE,
                                 ::base::BindOnce(&EncodeComponent::drainTask, mWeakThis, mode));
    return C2_OK;
}

c2_status_t EncodeComponent::flush_sm(flush_mode_t mode,
                                      std::list<std::unique_ptr<C2Work>>* const flushedWork) {
    ALOGV("%s()", __func__);

    if (mode != FLUSH_COMPONENT) {
        return C2_OMITTED;
    }

    if (mComponentState != ComponentState::RUNNING) {
        return C2_BAD_STATE;
    }

    ::base::WaitableEvent done;
    mEncoderTaskRunner->PostTask(FROM_HERE, ::base::BindOnce(&EncodeComponent::flushTask, mWeakThis,
                                                             &done, flushedWork));
    done.Wait();

    return C2_OK;
}

c2_status_t EncodeComponent::announce_nb(const std::vector<C2WorkOutline>& items) {
    return C2_OMITTED;
}

c2_status_t EncodeComponent::setListener_vb(const std::shared_ptr<Listener>& listener,
                                            c2_blocking_t mayBlock) {
    ALOG_ASSERT(mComponentState != ComponentState::UNLOADED);

    std::lock_guard<std::mutex> lock(mComponentLock);

    if (!mEncoderThread.IsRunning()) {
        mListener = listener;
        return C2_OK;
    }

    ALOG_ASSERT(mayBlock == c2_blocking_t::C2_MAY_BLOCK);

    ::base::WaitableEvent done;
    mEncoderTaskRunner->PostTask(FROM_HERE, ::base::BindOnce(&EncodeComponent::setListenerTask,
                                                             mWeakThis, listener, &done));
    done.Wait();

    return C2_OK;
}

std::shared_ptr<C2ComponentInterface> EncodeComponent::intf() {
    return std::make_shared<SimpleInterface<EncodeInterface>>(mName.c_str(), mId, mInterface);
}

void EncodeComponent::startTask(bool* success, ::base::WaitableEvent* done) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    *success = initializeEncoder();
    done->Signal();
}

void EncodeComponent::stopTask(::base::WaitableEvent* done) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    flush();

    mInputFormatConverter.reset();
    mInputPixelFormat = VideoPixelFormat::UNKNOWN;
    mInputLayout.clear();

    mEncoder.reset();
    mOutputBlockPool.reset();

    mWeakThisFactory.InvalidateWeakPtrs();

    done->Signal();
}

void EncodeComponent::queueTask(std::unique_ptr<C2Work> work) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mEncoder);

    auto failWork = [this, &work](c2_status_t status) {
        work->result = status;
        work->input.buffers.clear();
        work->workletsProcessed = 0;
        std::list<std::unique_ptr<C2Work>> failedWorks;
        failedWorks.emplace_back(std::move(work));
        mListener->onWorkDone_nb(weak_from_this(), std::move(failedWorks));
    };
    if (mComponentState == ComponentState::ERROR) {
        failWork(C2_BAD_STATE);
        return;
    }

    ALOG_ASSERT(work->input.buffers.size() <= 1u && work->worklets.size() == 1u);

    work->worklets.front()->output.flags = static_cast<C2FrameData::flags_t>(0);
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;

    uint64_t index = work->input.ordinal.frameIndex.peeku();
    int64_t timestamp = static_cast<int64_t>(work->input.ordinal.timestamp.peeku());
    bool endOfStream = work->input.flags & C2FrameData::FLAG_END_OF_STREAM;
    ALOGV("Queuing next encode (index: %" PRIu64 ", timestamp: %" PRId64 ", EOS: %d)", index,
          timestamp, endOfStream);

    if (!work->input.buffers.empty() && !work->input.buffers[0]) {
        ALOGD("Encountered null input buffer. Clearing the input buffer");
        work->input.buffers.clear();
    }

    if (work->input.buffers.empty() && !endOfStream) {
        ALOGV("Discarding empty CSD request");
        reportWork(std::move(work));
        return;
    }

    if (!mOutputBlockPool && !getBlockPool()) {
        reportError(C2_CORRUPTED);
        return;
    }

    if ((mInputPixelFormat == VideoPixelFormat::UNKNOWN) && !work->input.buffers.empty()) {
        const C2ConstGraphicBlock block =
                work->input.buffers.front()->data().graphicBlocks().front();
        VideoPixelFormat format = VideoPixelFormat::UNKNOWN;
        std::optional<std::vector<VideoFramePlane>> layout = getVideoFrameLayout(block, &format);
        if (!layout) {
            ALOGE("Failed to get input block's layout");
            reportError(C2_CORRUPTED);
            return;
        }
        ALOG_ASSERT(!mInputFormatConverter);
        if (!mEncoder->reconfigureInputFormat(format, *layout)) {
            mInputFormatConverter = FormatConverter::create(
                    mEncoder->inputFormat(), mEncoder->visibleSize(),
                    VideoEncoder::kInputBufferCount, mEncoder->codedSize());
            if (!mInputFormatConverter) {
                ALOGE("Failed to create input format converter");
                reportError(C2_CORRUPTED);
                return;
            }
        }
    }

    if (mInputFormatConverter && !mInputFormatConverter->isReady()) {
        ALOGV("Input format convertor ran out of buffers");
        mInputConverterQueue.push(std::move(work));
        return;
    }

    if (!work->input.buffers.empty()) {
        C2ConstGraphicBlock inputBlock =
                work->input.buffers.front()->data().graphicBlocks().front();
        if (mInputFormatConverter) {
            ALOGV("Converting input block (index: %" PRIu64 ")", index);
            c2_status_t status =
                    mInputFormatConverter->convertBlock(index, inputBlock, &inputBlock);
            if (status != C2_OK) {
                ALOGE("Failed to convert input block (index: %" PRIu64 ")", index);
                reportError(status);
                failWork(status);
                return;
            }
        } else {

            const C2Handle* handle = inputBlock.handle();
            uint32_t width, height, format, stride, generation, igbpSlot;
            uint64_t usage, igbpId;
            _UnwrapNativeCodec2GrallocMetadata(handle, &width, &height, &format, &usage, &stride,
                                               &generation, &igbpId, &igbpSlot);
            do {
                if (!(usage & GRALLOC_USAGE_SW_WRITE_MASK)) break;
                native_handle_t* gralloc_handle = UnwrapNativeCodec2GrallocHandle(handle);
                if (nullptr == gralloc_handle) break;
                sp<GraphicBuffer> buffer =
                        new GraphicBuffer(gralloc_handle, GraphicBuffer::CLONE_HANDLE, width,
                                          height, format, 1, usage, stride);
                native_handle_delete(gralloc_handle);
                void* pixels;
                if (buffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, &pixels)) break;
                buffer->unlock();
            } while (0);
        }
        if (!encode(inputBlock, index, timestamp)) {
            if (mInputFormatConverter) {
                mInputFormatConverter->returnBlock(index);
            }
            failWork(C2_CORRUPTED);
            return;
        }
    }

    mWorkQueue.push_back(std::move(work));
    if (endOfStream) {
        mEncoder->drain();
    }
}

void EncodeComponent::drainTask(drain_mode_t  ) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    if (!mInputConverterQueue.empty()) {
        C2Work* work = mInputConverterQueue.back().get();
        work->input.flags = static_cast<C2FrameData::flags_t>(work->input.flags |
                                                              C2FrameData::FLAG_END_OF_STREAM);
        return;
    }

    if (!mWorkQueue.empty()) {
        ALOGV("Starting drain and marking last item in output work queue as EOS");
        C2Work* work = mWorkQueue.back().get();
        work->input.flags = static_cast<C2FrameData::flags_t>(work->input.flags |
                                                              C2FrameData::FLAG_END_OF_STREAM);
        mEncoder->drain();
    }
}

void EncodeComponent::onDrainDone(bool success) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(!mWorkQueue.empty());

    if (!success) {
        ALOGE("draining the encoder failed");
        reportError(C2_CORRUPTED);
        return;
    }

    auto it = std::find_if(
            mWorkQueue.cbegin(), mWorkQueue.cend(), [](const std::unique_ptr<C2Work>& work) {
                return ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) &&
                        !(work->worklets.back()->output.flags & C2FrameData::FLAG_END_OF_STREAM));
            });
    if (it == mWorkQueue.end()) {
        ALOGW("No EOS work item found in queue");
        return;
    }

    C2Work* eosWork = it->get();
    eosWork->worklets.back()->output.flags = C2FrameData::FLAG_END_OF_STREAM;

    if ((eosWork != mWorkQueue.front().get()) || !isWorkDone(*eosWork)) {
        ALOGV("Draining done, waiting for input buffers to be returned");
        return;
    }

    ALOGV("Draining done");
    reportWork(std::move(mWorkQueue.front()));
    mWorkQueue.pop_front();
}

void EncodeComponent::flushTask(::base::WaitableEvent* done,
                                std::list<std::unique_ptr<C2Work>>* const flushedWork) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    if (flushedWork) {
        while (!mInputConverterQueue.empty()) {
            std::unique_ptr<C2Work> work = std::move(mInputConverterQueue.front());
            work->input.buffers.clear();
            flushedWork->push_back(std::move(work));
            mInputConverterQueue.pop();
        }
    }
    done->Signal();

    flush();
}

void EncodeComponent::setListenerTask(const std::shared_ptr<Listener>& listener,
                                      ::base::WaitableEvent* done) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    mListener = listener;
    done->Signal();
}

bool EncodeComponent::updateEncodingParameters() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    uint32_t bitrate = mInterface->getBitrate();
    if (mBitrate != bitrate) {
        ALOG_ASSERT(bitrate > 0u);
        ALOGV("Setting bitrate to %u", bitrate);
        if (!mEncoder->setBitrate(bitrate)) {
            reportError(C2_CORRUPTED);
            return false;
        }
        mBitrate = bitrate;

        if (mBitrateMode == C2Config::BITRATE_VARIABLE) {
            ALOGV("Setting peak bitrate to %u", bitrate * VideoEncoder::kPeakBitrateMultiplier);

            mEncoder->setPeakBitrate(bitrate * VideoEncoder::kPeakBitrateMultiplier);
        }
    }

    uint32_t framerate = static_cast<uint32_t>(std::round(mInterface->getFramerate()));
    if (mFramerate != framerate) {
        ALOG_ASSERT(framerate > 0u);
        ALOGV("Setting framerate to %u", framerate);
        if (!mEncoder->setFramerate(framerate)) {
            ALOGE("Requesting framerate change failed");
            reportError(C2_CORRUPTED);
            return false;
        }
        mFramerate = framerate;
    }

    C2StreamRequestSyncFrameTuning::output requestKeyFrame;
    c2_status_t status = mInterface->query({&requestKeyFrame}, {}, C2_DONT_BLOCK, nullptr);
    if (status != C2_OK) {
        ALOGE("Failed to query interface for key frame request (error code: %d)", status);
        reportError(status);
        return false;
    }
    if (requestKeyFrame.value == C2_TRUE) {
        mEncoder->requestKeyframe();
        requestKeyFrame.value = C2_FALSE;
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        status = mInterface->config({&requestKeyFrame}, C2_MAY_BLOCK, &failures);
        if (status != C2_OK) {
            ALOGE("Failed to reset key frame request on interface (error code: %d)", status);
            reportError(status);
            return false;
        }
    }

    C2Config::profile_t outputProfile = mInterface->getOutputProfile();
    if (isH264Profile(outputProfile)) {
        C2Config::level_t outputLevel = mInterface->getOutputLevel();
        ui::Size inputSize = mInterface->getInputVisibleSize();
        mMaxFramerate = maxFramerateForLevelH264(outputLevel, inputSize);
    }

    return true;
}

bool EncodeComponent::encode(C2ConstGraphicBlock block, uint64_t index, int64_t timestamp) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mEncoder);

    ALOGV("Encoding input block (index: %" PRIu64 ", timestamp: %" PRId64 ", size: %dx%d)", index,
          timestamp, block.width(), block.height());

    if (mInputPixelFormat == VideoPixelFormat::UNKNOWN) {
        ALOG_ASSERT(mInputLayout.empty());
        VideoPixelFormat format = VideoPixelFormat::UNKNOWN;
        std::optional<std::vector<VideoFramePlane>> inputLayout =
                getVideoFrameLayout(block, &format);
        if (!inputLayout) {
            ALOGE("Failed to get input block's layout");
            reportError(C2_CORRUPTED);
            return false;
        }
        if (!mEncoder->reconfigureInputFormat(format, *inputLayout)) {
            ALOGE("Encoder cannot use the converted input buffer layout");
            reportError(C2_BAD_VALUE);
            return false;
        }
        mInputPixelFormat = format;
        mInputLayout = std::move(*inputLayout);
    }

    constexpr int64_t kMaxFramerateDiff = 5;
    constexpr size_t kFramerateWindow = 30;
    constexpr size_t kFramerateMinSamples = 8;
    if (!mRecentFrameTimes.empty() && timestamp <= mRecentFrameTimes.back()) {
        mRecentFrameTimes.clear();
    }
    mRecentFrameTimes.push_back(timestamp);
    if (mRecentFrameTimes.size() > kFramerateWindow) {
        mRecentFrameTimes.pop_front();
    }
    if (mRecentFrameTimes.size() >= kFramerateMinSamples) {
        const int64_t span = mRecentFrameTimes.back() - mRecentFrameTimes.front();
        int64_t newFramerate = std::max(
                static_cast<int64_t>(std::round((mRecentFrameTimes.size() - 1) * 1000000.0 / span)),
                static_cast<int64_t>(1LL));

        if (newFramerate > mMaxFramerate) {
            ALOGW("Frames are coming too fast - new framerate (%" PRIi64
                  ") would exceed the maximum value (%" PRIu32 ")",
                  newFramerate, mMaxFramerate);
            newFramerate = mMaxFramerate;
        }

        if (abs(mFramerate - newFramerate) > kMaxFramerateDiff) {
            ALOGV("Adjusting framerate to %" PRId64 " based on frame timestamps", newFramerate);
            mInterface->setFramerate(static_cast<uint32_t>(newFramerate));
        }
    }

    if (!updateEncodingParameters()) return false;

    std::unique_ptr<VideoEncoder::InputFrame> frame =
            createInputFrame(block, mInputPixelFormat, mInputLayout, index, timestamp);
    if (!frame) {
        ALOGE("Failed to create video frame from input block (index: %" PRIu64
              ", timestamp: %" PRId64 ")",
              index, timestamp);
        reportError(C2_CORRUPTED);
        return false;
    }

    if (!mEncoder->encode(std::move(frame))) {
        return false;
    }

    return true;
}

void EncodeComponent::flush() {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    mEncoder->flush();

    std::list<std::unique_ptr<C2Work>> abortedWorkItems;
    while (!mInputConverterQueue.empty()) {
        std::unique_ptr<C2Work> work = std::move(mInputConverterQueue.front());
        work->result = C2_NOT_FOUND;
        work->input.buffers.clear();
        abortedWorkItems.push_back(std::move(work));
        mInputConverterQueue.pop();
    }
    while (!mWorkQueue.empty()) {
        std::unique_ptr<C2Work> work = std::move(mWorkQueue.front());

        if (mInputFormatConverter && work->input.buffers.empty()) {
            mInputFormatConverter->returnBlock(work->input.ordinal.frameIndex.peeku());
        }
        work->result = C2_NOT_FOUND;
        work->input.buffers.clear();
        abortedWorkItems.push_back(std::move(work));
        mWorkQueue.pop_front();
    }
    if (!abortedWorkItems.empty()) {
        mListener->onWorkDone_nb(weak_from_this(), std::move(abortedWorkItems));
    }
}

void EncodeComponent::fetchOutputBlock(uint32_t size, std::unique_ptr<BitstreamBuffer>* buffer) {
    ATRACE_CALL();
    ALOGV("Fetching linear block (size: %u)", size);
    std::shared_ptr<C2LinearBlock> block;
    c2_status_t status = mOutputBlockPool->fetchLinearBlock(
            size,
            C2MemoryUsage(C2MemoryUsage::CPU_READ |
                          static_cast<uint64_t>(BufferUsage::VIDEO_ENCODER)),
            &block);
    if (status != C2_OK) {
        ALOGE("Failed to fetch linear block (error: %d)", status);
        reportError(status);
    }

    *buffer = std::make_unique<BitstreamBuffer>(std::move(block), 0, size);
}

void EncodeComponent::onInputBufferDone(uint64_t index) {
    ALOGV("%s(): Input buffer done (index: %" PRIu64 ")", __func__, index);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mEncoder);

    C2Work* work = getWorkByIndex(index);
    if (!work) {
        ALOGE("Failed to find work associated with input buffer %" PRIu64, index);
        reportError(C2_CORRUPTED);
        return;
    }

    LOG_ASSERT(!work->input.buffers.empty());
    work->input.buffers.front().reset();

    if (mInputFormatConverter) {
        c2_status_t status = mInputFormatConverter->returnBlock(index);
        if (status != C2_OK) {
            reportError(status);
            return;
        }
        while (!mInputConverterQueue.empty() && mInputFormatConverter->isReady()) {
            std::unique_ptr<C2Work> work = std::move(mInputConverterQueue.front());
            mInputConverterQueue.pop();
            queueTask(std::move(work));
        }
    }

    while (!mWorkQueue.empty() && isWorkDone(*mWorkQueue.front())) {
        reportWork(std::move(mWorkQueue.front()));
        mWorkQueue.pop_front();
    }
}

void EncodeComponent::onOutputBufferDone(size_t dataSize, int64_t timestamp, bool keyFrame,
                                         std::unique_ptr<BitstreamBuffer> buffer) {
    ALOGV("%s(): output buffer done (timestamp: %" PRId64 ", size: %zu, keyframe: %d)", __func__,
          timestamp, dataSize, keyFrame);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(buffer->dmabuf);

    C2ConstLinearBlock constBlock =
            buffer->dmabuf->share(buffer->dmabuf->offset(), dataSize, C2Fence());

    if (mExtractCSD) {
        ALOGV("No CSD submitted yet, extracting CSD");
        std::unique_ptr<C2StreamInitDataInfo::output> csd;
        C2ReadView view = constBlock.map().get();
        bool isHevc = isValidProfileForCodec(VideoCodec::HEVC, mInterface->getOutputProfile());
        bool extracted = isHevc ? extractCSDInfoHevc(&csd, view.data(), view.capacity())
                                : extractCSDInfo(&csd, view.data(), view.capacity());
        if (!extracted) {
            ALOGE("Failed to extract CSD");
            reportError(C2_CORRUPTED);
            return;
        }

        LOG_ASSERT(!mWorkQueue.empty());
        C2Work* work = mWorkQueue.front().get();
        work->worklets.front()->output.configUpdate.push_back(std::move(csd));
        mExtractCSD = false;
    }

    C2Work* work = getWorkByTimestamp(timestamp);
    if (!work) {

        if (timestamp != 0) {
            reportError(C2_CORRUPTED);
        }
        return;
    }

    std::shared_ptr<C2Buffer> linearBuffer = C2Buffer::CreateLinearBuffer(std::move(constBlock));
    if (!linearBuffer) {
        ALOGE("Failed to create linear buffer from block");
        reportError(C2_CORRUPTED);
        return;
    }

    if (keyFrame) {
        linearBuffer->setInfo(
                std::make_shared<C2StreamPictureTypeMaskInfo::output>(0u, C2Config::SYNC_FRAME));
    }
    work->worklets.front()->output.buffers.emplace_back(std::move(linearBuffer));

    while (!mWorkQueue.empty() && isWorkDone(*mWorkQueue.front())) {
        reportWork(std::move(mWorkQueue.front()));
        mWorkQueue.pop_front();
    }
}

C2Work* EncodeComponent::getWorkByIndex(uint64_t index) {
    ALOGV("%s(): getting work item (index: %" PRIu64 ")", __func__, index);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    auto it = std::find_if(mWorkQueue.begin(), mWorkQueue.end(),
                           [index](const std::unique_ptr<C2Work>& w) {
                               return w->input.ordinal.frameIndex.peeku() == index;
                           });
    if (it == mWorkQueue.end()) {
        ALOGE("Failed to find work (index: %" PRIu64 ")", index);
        return nullptr;
    }
    return it->get();
}

C2Work* EncodeComponent::getWorkByTimestamp(int64_t timestamp) {
    ALOGV("%s(): getting work item (timestamp: %" PRId64 ")", __func__, timestamp);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(timestamp >= 0);

    auto it = std::find_if(
            mWorkQueue.begin(), mWorkQueue.end(), [timestamp](const std::unique_ptr<C2Work>& w) {
                return !(w->input.flags & C2FrameData::FLAG_END_OF_STREAM) &&
                       w->input.ordinal.timestamp.peeku() == static_cast<uint64_t>(timestamp);
            });
    if (it == mWorkQueue.end()) {
        ALOGE("Failed to find work (timestamp: %" PRIu64 ")", timestamp);
        return nullptr;
    }
    return it->get();
}

bool EncodeComponent::isWorkDone(const C2Work& work) const {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    if ((work.input.flags & C2FrameData::FLAG_END_OF_STREAM) &&
        !(work.worklets.front()->output.flags & C2FrameData::FLAG_END_OF_STREAM)) {
        ALOGV("Work item %" PRIu64 " is marked as EOS but draining has not finished yet",
              work.input.ordinal.frameIndex.peeku());
        return false;
    }

    if (!work.input.buffers.empty() && work.input.buffers.front()) {
        ALOGV("Input buffer associated with work item %" PRIu64 " not returned yet",
              work.input.ordinal.frameIndex.peeku());
        return false;
    }

    if (!work.input.buffers.empty() && work.worklets.front()->output.buffers.empty()) {
        ALOGV("Output buffer associated with work item %" PRIu64 " not returned yet",
              work.input.ordinal.frameIndex.peeku());
        return false;
    }

    return true;
}

void EncodeComponent::reportWork(std::unique_ptr<C2Work> work) {
    ATRACE_CALL();
    ALOG_ASSERT(work);
    ALOGV("%s(): Reporting work item as finished (index: %llu, timestamp: %llu)", __func__,
          work->input.ordinal.frameIndex.peekull(), work->input.ordinal.timestamp.peekull());
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    work->result = C2_OK;
    work->workletsProcessed = static_cast<uint32_t>(work->worklets.size());

    std::list<std::unique_ptr<C2Work>> finishedWorkList;
    finishedWorkList.emplace_back(std::move(work));
    mListener->onWorkDone_nb(weak_from_this(), std::move(finishedWorkList));
}

bool EncodeComponent::getBlockPool() {
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    auto sharedThis = weak_from_this().lock();
    if (!sharedThis) {
        ALOGI("%s(): V4L2EncodeComponent instance is already destroyed", __func__);
        return false;
    }

    C2BlockPool::local_id_t poolId = mInterface->getBlockPoolId();
    if (poolId == C2BlockPool::BASIC_LINEAR) {
        ALOGW("Using unoptimized linear block pool");
    }
    c2_status_t status = GetCodec2BlockPool(poolId, std::move(sharedThis), &mOutputBlockPool);
    if (status != C2_OK || !mOutputBlockPool) {
        ALOGE("Failed to get output block pool, error: %d", status);
        return false;
    }
    return true;
}

void EncodeComponent::reportError(c2_status_t error) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());

    std::lock_guard<std::mutex> lock(mComponentLock);
    if (mComponentState != ComponentState::ERROR) {
        setComponentState(ComponentState::ERROR);
        mListener->onError_nb(weak_from_this(), static_cast<uint32_t>(error));
    }
}

void EncodeComponent::setComponentState(ComponentState state) {

    switch (state) {
    case ComponentState::UNLOADED:
        ALOG_ASSERT(mComponentState == ComponentState::LOADED);
        break;
    case ComponentState::LOADED:
        ALOG_ASSERT(mComponentState == ComponentState::UNLOADED ||
                    mComponentState == ComponentState::RUNNING ||
                    mComponentState == ComponentState::ERROR);
        break;
    case ComponentState::RUNNING:
        ALOG_ASSERT(mComponentState == ComponentState::LOADED);
        break;
    case ComponentState::ERROR:
        break;
    }

    ALOGV("Changed component state from %s to %s", componentStateToString(mComponentState),
          componentStateToString(state));
    mComponentState = state;
}

const char* EncodeComponent::componentStateToString(EncodeComponent::ComponentState state) {
    switch (state) {
    case ComponentState::UNLOADED:
        return "UNLOADED";
    case ComponentState::LOADED:
        return "LOADED";
    case ComponentState::RUNNING:
        return "RUNNING";
    case ComponentState::ERROR:
        return "ERROR";
    }
}

}
