// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_ENCODE_INTERFACE_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_ENCODE_INTERFACE_H

#include <optional>
#include <vector>

#include <C2.h>
#include <C2Buffer.h>
#include <C2Config.h>
#include <ui/Size.h>
#include <util/C2InterfaceHelper.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/EncodeHelpers.h>

namespace media {
class V4L2Device;
};

namespace android {

class EncodeInterface : public C2InterfaceHelper {
public:
    EncodeInterface(const C2String& name, std::shared_ptr<C2ReflectorHelper> helper,
                    const SupportedCapabilities& caps);

    c2_status_t status() const { return mInitStatus; }
    const char* getOutputMediaType() const { return mOutputMediaType->m.value; }
    C2Config::profile_t getOutputProfile() const { return mProfileLevel->profile; }
    C2Config::level_t getOutputLevel() const { return mProfileLevel->level; }
    const ui::Size getInputVisibleSize() const {
        return ui::Size(mInputVisibleSize->width, mInputVisibleSize->height);
    }
    C2BlockPool::local_id_t getBlockPoolId() const { return mOutputBlockPoolIds->m.values[0]; }

    uint32_t getKeyFramePeriod() const;

    C2Config::bitrate_mode_t getBitrateMode() const { return mBitrateMode->value; }

    uint32_t getBitrate() const { return mBitrate->value; }

    float getFramerate() const { return mFrameRate->value; }

    void setFramerate(uint32_t framerate) { mFrameRate->value = framerate; }

protected:
    void Initialize(const C2String& name, const SupportedCapabilities& caps);

    static C2R H264ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::output>& info,
                                      const C2P<C2StreamPictureSizeInfo::input>& videosize,
                                      const C2P<C2StreamFrameRateInfo::output>& frameRate,
                                      const C2P<C2StreamBitrateInfo::output>& bitrate);
    static C2R VP9ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::output>& info,
                                     const C2P<C2StreamPictureSizeInfo::input>& videosize,
                                     const C2P<C2StreamFrameRateInfo::output>& frameRate,
                                     const C2P<C2StreamBitrateInfo::output>& bitrate);
    static C2R HEVCProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::output>& info,
                                      const C2P<C2StreamPictureSizeInfo::input>& videosize,
                                      const C2P<C2StreamFrameRateInfo::output>& frameRate,
                                      const C2P<C2StreamBitrateInfo::output>& bitrate);

    static C2R SizeSetter(bool mayBlock, C2P<C2StreamPictureSizeInfo::input>& videoSize);

    static C2R IntraRefreshPeriodSetter(bool mayBlock,
                                        C2P<C2StreamIntraRefreshTuning::output>& period);

    static C2Config::level_t lowestConfigLevel;

    std::shared_ptr<C2ComponentKindSetting> mKind;

    std::shared_ptr<C2StreamBufferTypeSetting::input> mInputFormat;

    std::shared_ptr<C2StreamUsageTuning::input> mInputMemoryUsage;

    std::shared_ptr<C2StreamBufferTypeSetting::output> mOutputFormat;

    std::shared_ptr<C2PortMediaTypeSetting::input> mInputMediaType;

    std::shared_ptr<C2PortMediaTypeSetting::output> mOutputMediaType;

    std::shared_ptr<C2PortAllocatorsTuning::input> mInputAllocatorIds;

    std::shared_ptr<C2PortAllocatorsTuning::output> mOutputAllocatorIds;

    std::shared_ptr<C2StreamPictureSizeInfo::input> mInputVisibleSize;

    std::shared_ptr<C2StreamProfileLevelInfo::output> mProfileLevel;

    std::shared_ptr<C2StreamSyncFrameIntervalTuning::output> mKeyFramePeriodUs;

    std::shared_ptr<C2PortBlockPoolsTuning::output> mOutputBlockPoolIds;

    std::shared_ptr<C2StreamBitrateInfo::output> mBitrate;

    std::shared_ptr<C2StreamBitrateModeTuning::output> mBitrateMode;

    std::shared_ptr<C2StreamFrameRateInfo::output> mFrameRate;

    std::shared_ptr<C2StreamRequestSyncFrameTuning::output> mRequestKeyFrame;

    std::shared_ptr<C2StreamIntraRefreshTuning::output> mIntraRefreshPeriod;

    c2_status_t mInitStatus = C2_NO_INIT;
};

}

#endif
