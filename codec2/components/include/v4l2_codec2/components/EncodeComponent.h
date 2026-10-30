// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_ENCODE_COMPONENT_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_ENCODE_COMPONENT_H

#include <atomic>
#include <limits>
#include <memory>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include <C2Component.h>
#include <C2ComponentFactory.h>
#include <C2Config.h>
#include <C2Enum.h>
#include <C2Param.h>
#include <C2ParamDef.h>
#include <SimpleC2Interface.h>
#include <base/memory/scoped_refptr.h>
#include <base/single_thread_task_runner.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>
#include <util/C2InterfaceHelper.h>
#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/VideoPixelFormat.h>

namespace android {

struct BitstreamBuffer;
class FormatConverter;
class VideoEncoder;
class EncodeInterface;

std::optional<std::vector<VideoFramePlane>> getVideoFrameLayout(const C2ConstGraphicBlock& block,
                                                                VideoPixelFormat* format);

std::optional<uint32_t> getVideoFrameStride(VideoPixelFormat format, ui::Size size);

class EncodeComponent : public C2Component, public std::enable_shared_from_this<EncodeComponent> {
public:
    virtual ~EncodeComponent() override;

    c2_status_t start() override;
    c2_status_t stop() override;
    c2_status_t reset() override;
    c2_status_t release() override;
    c2_status_t queue_nb(std::list<std::unique_ptr<C2Work>>* const items) override;
    c2_status_t drain_nb(drain_mode_t mode) override;
    c2_status_t flush_sm(flush_mode_t mode,
                         std::list<std::unique_ptr<C2Work>>* const flushedWork) override;
    c2_status_t announce_nb(const std::vector<C2WorkOutline>& items) override;
    c2_status_t setListener_vb(const std::shared_ptr<Listener>& listener,
                               c2_blocking_t mayBlock) override;
    std::shared_ptr<C2ComponentInterface> intf() override;

protected:

    enum class ComponentState {
        UNLOADED,
        LOADED,
        RUNNING,
        ERROR,
    };

    EncodeComponent(C2String name, c2_node_id_t id, std::shared_ptr<EncodeInterface> interface);

    EncodeComponent(const EncodeComponent&) = delete;
    EncodeComponent& operator=(const EncodeComponent&) = delete;

    virtual bool initializeEncoder() = 0;

    void startTask(bool* success, ::base::WaitableEvent* done);

    void stopTask(::base::WaitableEvent* done);

    void queueTask(std::unique_ptr<C2Work> work);

    void drainTask(drain_mode_t drainMode);

    void onDrainDone(bool success);

    void flushTask(::base::WaitableEvent* done,
                   std::list<std::unique_ptr<C2Work>>* const flushedWork);

    void setListenerTask(const std::shared_ptr<Listener>& listener, ::base::WaitableEvent* done);

    bool updateEncodingParameters();

    void scheduleNextEncodeTask();

    bool encode(C2ConstGraphicBlock block, uint64_t index, int64_t timestamp);

    void flush();

    void fetchOutputBlock(uint32_t size, std::unique_ptr<BitstreamBuffer>* buffer);

    void onInputBufferDone(uint64_t index);

    void onOutputBufferDone(size_t dataSize, int64_t timestamp, bool keyFrame,
                            std::unique_ptr<BitstreamBuffer> buffer);

    C2Work* getWorkByIndex(uint64_t index);

    C2Work* getWorkByTimestamp(int64_t timestamp);

    bool isWorkDone(const C2Work& work) const;

    void reportWork(std::unique_ptr<C2Work> work);

    bool getBlockPool();

    void reportError(c2_status_t error);

    void setComponentState(ComponentState state);

    static const char* componentStateToString(ComponentState state);

    std::unique_ptr<VideoEncoder> mEncoder;

    const C2String mName;

    const c2_node_id_t mId = 0;

    const std::shared_ptr<EncodeInterface> mInterface;

    std::mutex mComponentLock;

    std::shared_ptr<Listener> mListener;

    std::queue<std::unique_ptr<C2Work>> mInputConverterQueue;

    std::unique_ptr<FormatConverter> mInputFormatConverter;

    VideoPixelFormat mInputPixelFormat = VideoPixelFormat::UNKNOWN;

    std::vector<VideoFramePlane> mInputLayout;

    uint32_t mBitrate = 0;

    C2Config::bitrate_mode_t mBitrateMode = C2Config::BITRATE_CONST;

    uint32_t mFramerate = 0;

    uint32_t mMaxFramerate = std::numeric_limits<uint32_t>::max();

    std::deque<int64_t> mRecentFrameTimes;

    bool mExtractCSD = false;

    std::deque<std::unique_ptr<C2Work>> mWorkQueue;

    std::shared_ptr<C2BlockPool> mOutputBlockPool;

    std::atomic<ComponentState> mComponentState;

    ::base::Thread mEncoderThread{"EncodeComponentThread"};

    scoped_refptr<::base::SequencedTaskRunner> mEncoderTaskRunner;

    ::base::WeakPtr<EncodeComponent> mWeakThis;
    ::base::WeakPtrFactory<EncodeComponent> mWeakThisFactory{this};
};

}

#endif
