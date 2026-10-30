// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_DECODE_INTERFACE_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_DECODE_INTERFACE_H

#include <memory>
#include <string>

#include <C2Config.h>
#include <ui/Size.h>
#include <util/C2InterfaceHelper.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/VideoTypes.h>

namespace android {

class DecodeInterface : public C2InterfaceHelper {
public:
    DecodeInterface(const std::string& name, const std::shared_ptr<C2ReflectorHelper>& helper,
                    const SupportedCapabilities& caps);
    DecodeInterface(const DecodeInterface&) = delete;
    DecodeInterface& operator=(const DecodeInterface&) = delete;
    ~DecodeInterface() = default;

    c2_status_t status() const { return mInitStatus; }
    C2BlockPool::local_id_t getBlockPoolId() const { return mOutputBlockPoolIds->m.values[0]; }
    std::optional<VideoCodec> getVideoCodec() const { return mVideoCodec; }
    uint32_t getPixelFormat() const { return mPixelFormat->value; }

    static uint32_t getOutputDelay(VideoCodec codec);

    size_t getInputBufferSize() const;
    c2_status_t queryColorAspects(
            std::shared_ptr<C2StreamColorAspectsInfo::output>* targetColorAspects);

private:

    static C2R ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::input>& info);
    static C2R SizeSetter(bool mayBlock, C2P<C2StreamPictureSizeInfo::output>& videoSize);
    static C2R InputSizeSetter(bool mayBlock, C2P<C2StreamMaxBufferSizeInfo::input>& inputSize);
    static C2R MaxInputBufferSizeCalculator(bool mayBlock,
                                            C2P<C2StreamMaxBufferSizeInfo::input>& me,
                                            const C2P<C2StreamPictureSizeInfo::output>& size);

    template <typename T>
    static C2R DefaultColorAspectsSetter(bool mayBlock, C2P<T>& def);

    static C2R MergedColorAspectsSetter(bool mayBlock,
                                        C2P<C2StreamColorAspectsInfo::output>& merged,
                                        const C2P<C2StreamColorAspectsTuning::output>& def,
                                        const C2P<C2StreamColorAspectsInfo::input>& coded);

    std::shared_ptr<C2ComponentKindSetting> mKind;

    std::shared_ptr<C2StreamBufferTypeSetting::input> mInputFormat;

    std::shared_ptr<C2StreamUsageTuning::input> mInputMemoryUsage;

    std::shared_ptr<C2StreamBufferTypeSetting::output> mOutputFormat;

    std::shared_ptr<C2PortMediaTypeSetting::input> mInputMediaType;

    std::shared_ptr<C2PortMediaTypeSetting::output> mOutputMediaType;

    std::shared_ptr<C2PortDelayTuning::output> mOutputDelay;

    std::shared_ptr<C2PipelineDelayTuning> mPipelineDelay;

    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;

    std::shared_ptr<C2StreamPictureSizeInfo::output> mSize;

    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mMaxInputSize;

    std::shared_ptr<C2PortAllocatorsTuning::input> mInputAllocatorIds;

    std::shared_ptr<C2PortAllocatorsTuning::output> mOutputAllocatorIds;

    std::shared_ptr<C2PortSurfaceAllocatorTuning::output> mOutputSurfaceAllocatorId;

    std::shared_ptr<C2PortBlockPoolsTuning::output> mOutputBlockPoolIds;

    std::shared_ptr<C2StreamColorAspectsInfo::input> mCodedColorAspects;

    std::shared_ptr<C2StreamColorAspectsTuning::output> mDefaultColorAspects;

    std::shared_ptr<C2StreamColorAspectsInfo::output> mColorAspects;
    std::shared_ptr<C2StreamPixelFormatInfo::output> mPixelFormat;

    c2_status_t mInitStatus;
    std::optional<VideoCodec> mVideoCodec;
};

}

#endif
