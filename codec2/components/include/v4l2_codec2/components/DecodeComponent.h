// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_DECODE_COMPONENT_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_DECODE_COMPONENT_H

#include <atomic>
#include <memory>

#include <C2Component.h>
#include <C2ComponentFactory.h>
#include <C2Work.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>

#include <v4l2_codec2/components/DecodeInterface.h>
#include <v4l2_codec2/components/VideoDecoder.h>
#include <v4l2_codec2/components/VideoFramePool.h>

namespace android {

class DecodeComponent : public C2Component, public std::enable_shared_from_this<DecodeComponent> {
public:
    DecodeComponent(uint32_t debugStreamId, const std::string& name, c2_node_id_t id,
                    const std::shared_ptr<DecodeInterface>& intfImpl);
    virtual ~DecodeComponent() override;

    c2_status_t start() override;
    c2_status_t stop() override;
    c2_status_t reset() override;
    c2_status_t release() override;
    c2_status_t setListener_vb(const std::shared_ptr<Listener>& listener,
                               c2_blocking_t mayBlock) override;
    c2_status_t queue_nb(std::list<std::unique_ptr<C2Work>>* const items) override;
    c2_status_t announce_nb(const std::vector<C2WorkOutline>& items) override;
    c2_status_t flush_sm(flush_mode_t mode,
                         std::list<std::unique_ptr<C2Work>>* const flushedWork) override;
    c2_status_t drain_nb(drain_mode_t mode) override;
    std::shared_ptr<C2ComponentInterface> intf() override;

protected:

    enum class ComponentState {
        STOPPED,
        RUNNING,
        RELEASED,
        ERROR,
    };
    static const char* ComponentStateToString(ComponentState state);

    virtual void startTask(c2_status_t* status, ::base::WaitableEvent* done) = 0;
    void stopTask();
    void releaseTask();
    void queueTask(std::unique_ptr<C2Work> work);
    void flushTask();
    void drainTask();
    void setListenerTask(const std::shared_ptr<Listener>& listener, ::base::WaitableEvent* done);

    void pumpPendingWorks();

    void processCSDWork(const int32_t bitstreamId, const C2Work* work);
    void processWork(const int32_t bitstreamId, const C2Work* work);
    void processWorkBuffer(const int32_t bitstreamId, const C2ConstLinearBlock& linearBlock);

    std::unique_ptr<VideoFramePool> getVideoFramePool(const ui::Size& size,
                                                      HalPixelFormat pixelFormat,
                                                      size_t numBuffers);

    void detectNoShowFrameWorksAndReportIfFinished(const C2WorkOrdinalStruct& currOrdinal);

    void onOutputFrameReady(std::unique_ptr<VideoFrame> frame);
    void onDecodeDone(int32_t bitstreamId, VideoDecoder::DecodeStatus status);
    void onDrainDone(VideoDecoder::DecodeStatus status);
    void onFlushDone();

    void pumpReportWork();

    bool reportWorkIfFinished(int32_t bitstreamId);
    bool reportEOSWork();
    void reportAbandonedWorks();
    bool reportWork(std::unique_ptr<C2Work> work);

    void reportError(c2_status_t error);

    uint32_t mDebugStreamId;

    std::shared_ptr<DecodeInterface> mIntfImpl;

    const std::shared_ptr<C2ComponentInterface> mIntf;

    std::shared_ptr<Listener> mListener;

    std::unique_ptr<VideoDecoder> mDecoder;

    std::queue<std::unique_ptr<C2Work>> mPendingWorks;

    std::map<int32_t, std::unique_ptr<C2Work>> mWorksAtDecoder;

    std::queue<int32_t> mOutputBitstreamIds;

    bool mIsSecure = false;

    std::atomic<ComponentState> mComponentState{ComponentState::STOPPED};

    bool mIsDraining = false;

    std::mutex mStartStopLock;

    std::shared_ptr<C2StreamColorAspectsInfo::output> mCurrentColorAspects;

    bool mPendingColorAspectsChange = false;

    uint64_t mPendingColorAspectsChangeFrameIndex;

    ::base::Thread mDecoderThread{"DecodeComponentDecoderThread"};
    scoped_refptr<::base::SequencedTaskRunner> mDecoderTaskRunner;

    ::base::WeakPtrFactory<DecodeComponent> mWeakThisFactory{this};
    ::base::WeakPtr<DecodeComponent> mWeakThis;
};

}

#endif
