// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "V4L2DecodeComponent"

#include <v4l2_codec2/v4l2/V4L2DecodeComponent.h>
#include <v4l2_codec2/v4l2/V4L2Decoder.h>

#include <base/bind.h>
#include <base/callback_helpers.h>

#include <cutils/properties.h>
#include <utils/Trace.h>

namespace android {

namespace {

size_t getMinNumOutputBuffers(VideoCodec codec) {

    constexpr size_t kSmoothnessFactor = 4;
    constexpr size_t kRenderingDepth = 3;

    constexpr size_t kExtraNumOutputBuffersForDecoder = 2;

    return DecodeInterface::getOutputDelay(codec) + kSmoothnessFactor + kRenderingDepth +
           kExtraNumOutputBuffersForDecoder;
}
}

std::atomic<int32_t> V4L2DecodeComponent::sConcurrentInstances = 0;

std::atomic<uint32_t> V4L2DecodeComponent::sNextDebugStreamId = 0;

std::shared_ptr<C2Component> V4L2DecodeComponent::create(
        const std::string& name, c2_node_id_t id, std::shared_ptr<DecodeInterface> intfImpl,
        C2ComponentFactory::ComponentDeleter deleter) {
    static const int32_t kMaxConcurrentInstances =
            property_get_int32("ro.vendor.v4l2_codec2.decode_concurrent_instances", -1);
    static std::mutex mutex;

    std::lock_guard<std::mutex> lock(mutex);

    if (kMaxConcurrentInstances >= 0 && sConcurrentInstances.load() >= kMaxConcurrentInstances) {
        ALOGW("Reject to Initialize() due to too many instances: %d", sConcurrentInstances.load());
        return nullptr;
    } else if (sConcurrentInstances.load() == 0) {
        sNextDebugStreamId.store(0, std::memory_order_relaxed);
    }

    uint32_t debugStreamId = sNextDebugStreamId.fetch_add(1, std::memory_order_relaxed);
    return std::shared_ptr<C2Component>(
            new V4L2DecodeComponent(debugStreamId, name, id, std::move(intfImpl)), deleter);
}

V4L2DecodeComponent::V4L2DecodeComponent(uint32_t debugStreamId, const std::string& name,
                                         c2_node_id_t id, std::shared_ptr<DecodeInterface> intfImpl)
      : DecodeComponent(debugStreamId, name, id, intfImpl) {
    ALOGV("%s(): ", __func__);
    sConcurrentInstances.fetch_add(1, std::memory_order_relaxed);
}

V4L2DecodeComponent::~V4L2DecodeComponent() {
    ALOGV("%s(): ", __func__);
    sConcurrentInstances.fetch_sub(1, std::memory_order_relaxed);
}

void V4L2DecodeComponent::startTask(c2_status_t* status, ::base::WaitableEvent* done) {
    ATRACE_CALL();
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mDecoderTaskRunner->RunsTasksInCurrentSequence());

    ::base::ScopedClosureRunner done_caller(
            ::base::BindOnce(&::base::WaitableEvent::Signal, ::base::Unretained(done)));
    *status = C2_CORRUPTED;

    const auto codec = mIntfImpl->getVideoCodec();
    if (!codec) {
        ALOGE("Failed to get video codec.");
        return;
    }
    const size_t inputBufferSize = mIntfImpl->getInputBufferSize();
    const size_t minNumOutputBuffers = getMinNumOutputBuffers(*codec);

    mDecoder = V4L2Decoder::Create(mDebugStreamId, *codec, inputBufferSize, minNumOutputBuffers,
                                   ::base::BindRepeating(&V4L2DecodeComponent::getVideoFramePool,
                                                         ::base::Unretained(this)),
                                   ::base::BindRepeating(&V4L2DecodeComponent::onOutputFrameReady,
                                                         ::base::Unretained(this)),
                                   ::base::BindRepeating(&V4L2DecodeComponent::reportError,
                                                         ::base::Unretained(this), C2_CORRUPTED),
                                   mDecoderTaskRunner, mIsSecure, mIntfImpl->getPixelFormat());
    if (!mDecoder) {
        ALOGE("Failed to create V4L2Decoder for %s", VideoCodecToString(*codec));
        return;
    }

    if (!mIsSecure && *codec == VideoCodec::H264) {
        if (mIntfImpl->queryColorAspects(&mCurrentColorAspects) != C2_OK) return;
        mPendingColorAspectsChange = false;
    }

    *status = C2_OK;
}

}
