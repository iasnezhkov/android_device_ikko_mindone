// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "DecodeInterface"

#include <v4l2_codec2/components/DecodeInterface.h>

#include <C2PlatformSupport.h>
#include <SimpleC2Interface.h>
#include <android/hardware/graphics/common/1.0/types.h>
#include <log/log.h>
#include <media/stagefright/foundation/MediaDefs.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/plugin_store/V4L2AllocatorId.h>

namespace android {
namespace {

constexpr size_t k1080pArea = 1920 * 1088;
constexpr size_t k4KArea = 3840 * 2160;

constexpr size_t kInputBufferSizeFor1080p = 16 * 1024 * 1024;

constexpr size_t kInputBufferSizeFor4K = 4 * kInputBufferSizeFor1080p;

size_t calculateInputBufferSize(size_t area) {
    if (area > k4KArea) {
        ALOGW("Input buffer size for video size (%zu) larger than 4K (%zu) might be too small.",
              area, k4KArea);
    }

    if (area > k1080pArea) return kInputBufferSizeFor4K;
    return kInputBufferSizeFor1080p;
}

bool is10BitProfile(uint32_t profile) {
    switch (profile) {
    case C2Config::PROFILE_HEVC_MAIN_10:
    case C2Config::PROFILE_VP9_2:
    case C2Config::PROFILE_VP9_3:
        return true;
    default:
        return false;
    }
}
}

C2R DecodeInterface::ProfileLevelSetter(bool  ,
                                        C2P<C2StreamProfileLevelInfo::input>& info) {
    return info.F(info.v.profile)
            .validatePossible(info.v.profile)
            .plus(info.F(info.v.level).validatePossible(info.v.level));
}

C2R DecodeInterface::SizeSetter(bool  ,
                                C2P<C2StreamPictureSizeInfo::output>& videoSize) {
    return videoSize.F(videoSize.v.width)
            .validatePossible(videoSize.v.width)
            .plus(videoSize.F(videoSize.v.height).validatePossible(videoSize.v.height));
}

C2R DecodeInterface::InputSizeSetter(bool  ,
                                     C2P<C2StreamMaxBufferSizeInfo::input>& inputSize) {
    return inputSize.F(inputSize.v.value).validatePossible(inputSize.v.value);
}

template <typename T>
C2R DecodeInterface::DefaultColorAspectsSetter(bool  , C2P<T>& def) {
    if (def.v.range > C2Color::RANGE_OTHER) {
        def.set().range = C2Color::RANGE_OTHER;
    }
    if (def.v.primaries > C2Color::PRIMARIES_OTHER) {
        def.set().primaries = C2Color::PRIMARIES_OTHER;
    }
    if (def.v.transfer > C2Color::TRANSFER_OTHER) {
        def.set().transfer = C2Color::TRANSFER_OTHER;
    }
    if (def.v.matrix > C2Color::MATRIX_OTHER) {
        def.set().matrix = C2Color::MATRIX_OTHER;
    }
    return C2R::Ok();
}

C2R DecodeInterface::MergedColorAspectsSetter(bool  ,
                                              C2P<C2StreamColorAspectsInfo::output>& merged,
                                              const C2P<C2StreamColorAspectsTuning::output>& def,
                                              const C2P<C2StreamColorAspectsInfo::input>& coded) {

    merged.set().range = coded.v.range == RANGE_UNSPECIFIED ? def.v.range : coded.v.range;
    merged.set().primaries =
            coded.v.primaries == PRIMARIES_UNSPECIFIED ? def.v.primaries : coded.v.primaries;
    merged.set().transfer =
            coded.v.transfer == TRANSFER_UNSPECIFIED ? def.v.transfer : coded.v.transfer;
    merged.set().matrix = coded.v.matrix == MATRIX_UNSPECIFIED ? def.v.matrix : coded.v.matrix;
    return C2R::Ok();
}

C2R DecodeInterface::MaxInputBufferSizeCalculator(
        bool  , C2P<C2StreamMaxBufferSizeInfo::input>& me,
        const C2P<C2StreamPictureSizeInfo::output>& size) {
    size_t calculatedSize = calculateInputBufferSize(size.v.width * size.v.height);

    if (me.v.value < calculatedSize) me.set().value = calculatedSize;

    return C2R::Ok();
}

DecodeInterface::DecodeInterface(const std::string& name,
                                 const std::shared_ptr<C2ReflectorHelper>& helper,
                                 const SupportedCapabilities& caps)
      : C2InterfaceHelper(helper), mInitStatus(C2_OK), mVideoCodec(caps.codec) {
    ALOGV("%s(%s)", __func__, name.c_str());

    setDerivedInstance(this);

    addParameter(DefineParam(mKind, C2_PARAMKEY_COMPONENT_KIND)
                         .withConstValue(new C2ComponentKindSetting(C2Component::KIND_DECODER))
                         .build());

    std::string inputMime;

    ui::Size maxSize(1, 1);

    std::vector<uint32_t> profiles;
    for (const auto& supportedProfile : caps.supportedProfiles) {
        if (isValidProfileForCodec(mVideoCodec.value(), supportedProfile.profile)) {
            profiles.push_back(static_cast<uint32_t>(supportedProfile.profile));
            maxSize.setWidth(std::max(maxSize.width, supportedProfile.max_resolution.width));
            maxSize.setHeight(std::max(maxSize.height, supportedProfile.max_resolution.height));
        }
    }

    if (maxSize == ui::Size(1, 1)) maxSize = ui::Size(4096, 4096);

    if (profiles.empty()) {
        ALOGW("No supported profiles for H264 codec");
        switch (*mVideoCodec) {
        case VideoCodec::H264:
            profiles = {
                    C2Config::PROFILE_AVC_BASELINE,
                    C2Config::PROFILE_AVC_CONSTRAINED_BASELINE,
                    C2Config::PROFILE_AVC_MAIN,
                    C2Config::PROFILE_AVC_HIGH,
            };
            break;
        case VideoCodec::VP8:
            profiles = {C2Config::PROFILE_VP8_0};
            break;
        case VideoCodec::VP9:
            profiles = {C2Config::PROFILE_VP9_0};
            break;
        case VideoCodec::HEVC:
            profiles = {C2Config::PROFILE_HEVC_MAIN};
            break;
        case VideoCodec::AV1:
            profiles = {C2Config::PROFILE_AV1_0};
            break;
        case VideoCodec::MPEG4:
            profiles = {
                    C2Config::PROFILE_MP4V_SIMPLE,
                    C2Config::PROFILE_MP4V_ADVANCED_SIMPLE,
            };
            break;
        case VideoCodec::H263:
            profiles = {C2Config::PROFILE_H263_BASELINE};
            break;
        }
    }

    const bool has10BitProfile = std::any_of(profiles.begin(), profiles.end(), is10BitProfile);

    uint32_t defaultProfile = caps.defaultProfile;
    if (defaultProfile == C2Config::PROFILE_UNUSED)
        defaultProfile = *std::min_element(profiles.begin(), profiles.end());

    std::vector<unsigned int> levels;
    std::vector<C2Config::level_t> supportedLevels = caps.supportedLevels;
    for (const auto& supportedLevel : supportedLevels) {
        levels.push_back(static_cast<unsigned int>(supportedLevel));
    }

    if (levels.empty()) {
        ALOGE("No supported levels for H264 codec");
        switch (*mVideoCodec) {
        case VideoCodec::H264:
            levels = {C2Config::LEVEL_AVC_1,   C2Config::LEVEL_AVC_1B,  C2Config::LEVEL_AVC_1_1,
                      C2Config::LEVEL_AVC_1_2, C2Config::LEVEL_AVC_1_3, C2Config::LEVEL_AVC_2,
                      C2Config::LEVEL_AVC_2_1, C2Config::LEVEL_AVC_2_2, C2Config::LEVEL_AVC_3,
                      C2Config::LEVEL_AVC_3_1, C2Config::LEVEL_AVC_3_2, C2Config::LEVEL_AVC_4,
                      C2Config::LEVEL_AVC_4_1, C2Config::LEVEL_AVC_4_2, C2Config::LEVEL_AVC_5,
                      C2Config::LEVEL_AVC_5_1, C2Config::LEVEL_AVC_5_2};
            break;
        case VideoCodec::VP8:
            levels = {C2Config::LEVEL_UNUSED};
            break;
        case VideoCodec::VP9:
            levels = {C2Config::LEVEL_VP9_1,   C2Config::LEVEL_VP9_1_1, C2Config::LEVEL_VP9_2,
                      C2Config::LEVEL_VP9_2_1, C2Config::LEVEL_VP9_3,   C2Config::LEVEL_VP9_3_1,
                      C2Config::LEVEL_VP9_4,   C2Config::LEVEL_VP9_4_1, C2Config::LEVEL_VP9_5};
            break;
        case VideoCodec::HEVC:
            levels = {C2Config::LEVEL_HEVC_MAIN_1,   C2Config::LEVEL_HEVC_MAIN_2,
                      C2Config::LEVEL_HEVC_MAIN_2_1, C2Config::LEVEL_HEVC_MAIN_3,
                      C2Config::LEVEL_HEVC_MAIN_3_1, C2Config::LEVEL_HEVC_MAIN_4,
                      C2Config::LEVEL_HEVC_MAIN_4_1, C2Config::LEVEL_HEVC_MAIN_5,
                      C2Config::LEVEL_HEVC_MAIN_5_1, C2Config::LEVEL_HEVC_MAIN_5_2,
                      C2Config::LEVEL_HEVC_MAIN_6,   C2Config::LEVEL_HEVC_MAIN_6_1,
                      C2Config::LEVEL_HEVC_MAIN_6_2};
            break;
        case VideoCodec::AV1:
            levels = {C2Config::LEVEL_AV1_2, C2Config::LEVEL_AV1_2_1, C2Config::LEVEL_AV1_2_2, C2Config::LEVEL_AV1_2_3,
                      C2Config::LEVEL_AV1_3, C2Config::LEVEL_AV1_3_1, C2Config::LEVEL_AV1_3_2, C2Config::LEVEL_AV1_3_3,
                      C2Config::LEVEL_AV1_4, C2Config::LEVEL_AV1_4_1, C2Config::LEVEL_AV1_4_2, C2Config::LEVEL_AV1_4_3,
                      C2Config::LEVEL_AV1_5, C2Config::LEVEL_AV1_5_1, C2Config::LEVEL_AV1_5_2, C2Config::LEVEL_AV1_5_3,
                      C2Config::LEVEL_AV1_6, C2Config::LEVEL_AV1_6_1, C2Config::LEVEL_AV1_6_2, C2Config::LEVEL_AV1_6_3,
                      C2Config::LEVEL_AV1_7, C2Config::LEVEL_AV1_7_1, C2Config::LEVEL_AV1_7_2, C2Config::LEVEL_AV1_7_3};
            break;
        case VideoCodec::MPEG4:
            levels = {C2Config::LEVEL_MP4V_0,  C2Config::LEVEL_MP4V_0B, C2Config::LEVEL_MP4V_1,
                      C2Config::LEVEL_MP4V_2,  C2Config::LEVEL_MP4V_3,  C2Config::LEVEL_MP4V_4,
                      C2Config::LEVEL_MP4V_5};
            break;
        case VideoCodec::H263:
            levels = {C2Config::LEVEL_H263_10, C2Config::LEVEL_H263_20, C2Config::LEVEL_H263_30,
                      C2Config::LEVEL_H263_40, C2Config::LEVEL_H263_45, C2Config::LEVEL_H263_50,
                      C2Config::LEVEL_H263_60, C2Config::LEVEL_H263_70};
            break;
        }
    }

    uint32_t defaultLevel = caps.defaultLevel;
    if (defaultLevel == C2Config::LEVEL_UNUSED)
        defaultLevel = *std::min_element(levels.begin(), levels.end());

    switch (*mVideoCodec) {
    case VideoCodec::H264:
        inputMime = MEDIA_MIMETYPE_VIDEO_AVC;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withDefault(new C2StreamProfileLevelInfo::input(
                                     0u, static_cast<C2Config::profile_t>(defaultProfile),
                                     static_cast<C2Config::level_t>(defaultLevel)))
                             .withFields({C2F(mProfileLevel, profile).oneOf(profiles),
                                          C2F(mProfileLevel, level).oneOf(levels)})
                             .withSetter(ProfileLevelSetter)
                             .build());
        break;

    case VideoCodec::VP8:
        inputMime = MEDIA_MIMETYPE_VIDEO_VP8;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withConstValue(new C2StreamProfileLevelInfo::input(
                                     0u, C2Config::PROFILE_UNUSED, C2Config::LEVEL_UNUSED))
                             .build());
        break;

    case VideoCodec::VP9:
        inputMime = MEDIA_MIMETYPE_VIDEO_VP9;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withDefault(new C2StreamProfileLevelInfo::input(
                                     0u, static_cast<C2Config::profile_t>(defaultProfile),
                                     static_cast<C2Config::level_t>(defaultLevel)))
                             .withFields({C2F(mProfileLevel, profile).oneOf(profiles),
                                          C2F(mProfileLevel, level).oneOf(levels)})
                             .withSetter(ProfileLevelSetter)
                             .build());
        break;

    case VideoCodec::HEVC:
        inputMime = MEDIA_MIMETYPE_VIDEO_HEVC;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withDefault(new C2StreamProfileLevelInfo::input(
                                     0u, static_cast<C2Config::profile_t>(defaultProfile),
                                     static_cast<C2Config::level_t>(defaultLevel)))
                             .withFields({C2F(mProfileLevel, profile).oneOf(profiles),
                                          C2F(mProfileLevel, level).oneOf(levels)})
                             .withSetter(ProfileLevelSetter)
                             .build());
        break;

    case VideoCodec::AV1:
        inputMime = MEDIA_MIMETYPE_VIDEO_AV1;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withDefault(new C2StreamProfileLevelInfo::input(
                                     0u, static_cast<C2Config::profile_t>(defaultProfile),
                                     static_cast<C2Config::level_t>(defaultLevel)))
                             .withFields({C2F(mProfileLevel, profile).oneOf(profiles),
                                          C2F(mProfileLevel, level).oneOf(levels)})
                             .withSetter(ProfileLevelSetter)
                             .build());
        break;

    case VideoCodec::MPEG4:
        inputMime = MEDIA_MIMETYPE_VIDEO_MPEG4;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withDefault(new C2StreamProfileLevelInfo::input(
                                     0u, static_cast<C2Config::profile_t>(defaultProfile),
                                     static_cast<C2Config::level_t>(defaultLevel)))
                             .withFields({C2F(mProfileLevel, profile).oneOf(profiles),
                                          C2F(mProfileLevel, level).oneOf(levels)})
                             .withSetter(ProfileLevelSetter)
                             .build());
        break;

    case VideoCodec::H263:
        inputMime = MEDIA_MIMETYPE_VIDEO_H263;
        addParameter(DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                             .withDefault(new C2StreamProfileLevelInfo::input(
                                     0u, static_cast<C2Config::profile_t>(defaultProfile),
                                     static_cast<C2Config::level_t>(defaultLevel)))
                             .withFields({C2F(mProfileLevel, profile).oneOf(profiles),
                                          C2F(mProfileLevel, level).oneOf(levels)})
                             .withSetter(ProfileLevelSetter)
                             .build());
        break;
    }

    addParameter(
            DefineParam(mInputFormat, C2_PARAMKEY_INPUT_STREAM_BUFFER_TYPE)
                    .withConstValue(new C2StreamBufferTypeSetting::input(0u, C2BufferData::LINEAR))
                    .build());
    addParameter(
            DefineParam(mInputMemoryUsage, C2_PARAMKEY_INPUT_STREAM_USAGE)
                    .withConstValue(new C2StreamUsageTuning::input(
                            0u, static_cast<uint64_t>(android::hardware::graphics::common::V1_0::
                                                              BufferUsage::VIDEO_DECODER)))
                    .build());

    addParameter(DefineParam(mOutputFormat, C2_PARAMKEY_OUTPUT_STREAM_BUFFER_TYPE)
                         .withConstValue(
                                 new C2StreamBufferTypeSetting::output(0u, C2BufferData::GRAPHIC))
                         .build());
    addParameter(
            DefineParam(mOutputDelay, C2_PARAMKEY_OUTPUT_DELAY)
                    .withConstValue(new C2PortDelayTuning::output(getOutputDelay(*mVideoCodec)))
                    .build());

    addParameter(DefineParam(mPipelineDelay, C2_PARAMKEY_PIPELINE_DELAY)
                         .withConstValue(new C2PipelineDelayTuning(3))
                         .build());

    addParameter(DefineParam(mInputMediaType, C2_PARAMKEY_INPUT_MEDIA_TYPE)
                         .withConstValue(AllocSharedString<C2PortMediaTypeSetting::input>(
                                 inputMime.c_str()))
                         .build());

    addParameter(DefineParam(mOutputMediaType, C2_PARAMKEY_OUTPUT_MEDIA_TYPE)
                         .withConstValue(AllocSharedString<C2PortMediaTypeSetting::output>(
                                 MEDIA_MIMETYPE_VIDEO_RAW))
                         .build());

    addParameter(DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                         .withDefault(new C2StreamPictureSizeInfo::output(
                                 0u, std::min(320, maxSize.width), std::min(240, maxSize.height)))
                         .withFields({
                                 C2F(mSize, width).inRange(16, maxSize.width, 16),
                                 C2F(mSize, height).inRange(16, maxSize.height, 16),
                         })
                         .withSetter(SizeSetter)
                         .build());

    addParameter(
            DefineParam(mMaxInputSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                    .withDefault(new C2StreamMaxBufferSizeInfo::input(0u, kInputBufferSizeFor1080p))
                    .withFields({
                            C2F(mMaxInputSize, value).any(),
                    })
                    .withSetter(InputSizeSetter)
                    .calculatedAs(MaxInputBufferSizeCalculator, mSize)
                    .build());

    bool secureMode = name.find(".secure") != std::string::npos;
    const C2Allocator::id_t inputAllocators[] = {secureMode ? V4L2AllocatorId::SECURE_LINEAR
                                                            : C2AllocatorStore::DEFAULT_LINEAR};

    const C2Allocator::id_t outputAllocators[] = {C2PlatformAllocatorStore::GRALLOC};
    const C2Allocator::id_t surfaceAllocator =
            secureMode ? V4L2AllocatorId::SECURE_GRAPHIC : C2PlatformAllocatorStore::BUFFERQUEUE;
    const C2BlockPool::local_id_t outputBlockPools[] = {C2BlockPool::BASIC_GRAPHIC};

    addParameter(
            DefineParam(mInputAllocatorIds, C2_PARAMKEY_INPUT_ALLOCATORS)
                    .withConstValue(C2PortAllocatorsTuning::input::AllocShared(inputAllocators))
                    .build());

    addParameter(
            DefineParam(mOutputAllocatorIds, C2_PARAMKEY_OUTPUT_ALLOCATORS)
                    .withConstValue(C2PortAllocatorsTuning::output::AllocShared(outputAllocators))
                    .build());

    addParameter(DefineParam(mOutputSurfaceAllocatorId, C2_PARAMKEY_OUTPUT_SURFACE_ALLOCATOR)
                         .withConstValue(new C2PortSurfaceAllocatorTuning::output(surfaceAllocator))
                         .build());

    addParameter(
            DefineParam(mOutputBlockPoolIds, C2_PARAMKEY_OUTPUT_BLOCK_POOLS)
                    .withDefault(C2PortBlockPoolsTuning::output::AllocShared(outputBlockPools))
                    .withFields({C2F(mOutputBlockPoolIds, m.values[0]).any(),
                                 C2F(mOutputBlockPoolIds, m.values).inRange(0, 1)})
                    .withSetter(Setter<C2PortBlockPoolsTuning::output>::NonStrictValuesWithNoDeps)
                    .build());

    addParameter(
            DefineParam(mDefaultColorAspects, C2_PARAMKEY_DEFAULT_COLOR_ASPECTS)
                    .withDefault(new C2StreamColorAspectsTuning::output(
                            0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                            C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                    .withFields(
                            {C2F(mDefaultColorAspects, range)
                                     .inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                             C2F(mDefaultColorAspects, primaries)
                                     .inRange(C2Color::PRIMARIES_UNSPECIFIED,
                                              C2Color::PRIMARIES_OTHER),
                             C2F(mDefaultColorAspects, transfer)
                                     .inRange(C2Color::TRANSFER_UNSPECIFIED,
                                              C2Color::TRANSFER_OTHER),
                             C2F(mDefaultColorAspects, matrix)
                                     .inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER)})
                    .withSetter(DefaultColorAspectsSetter)
                    .build());

    addParameter(
            DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
                    .withDefault(new C2StreamColorAspectsInfo::input(
                            0u, C2Color::RANGE_LIMITED, C2Color::PRIMARIES_UNSPECIFIED,
                            C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                    .withFields(
                            {C2F(mCodedColorAspects, range)
                                     .inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                             C2F(mCodedColorAspects, primaries)
                                     .inRange(C2Color::PRIMARIES_UNSPECIFIED,
                                              C2Color::PRIMARIES_OTHER),
                             C2F(mCodedColorAspects, transfer)
                                     .inRange(C2Color::TRANSFER_UNSPECIFIED,
                                              C2Color::TRANSFER_OTHER),
                             C2F(mCodedColorAspects, matrix)
                                     .inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER)})
                    .withSetter(DefaultColorAspectsSetter)
                    .build());

    if ((mVideoCodec == VideoCodec::H264 && !secureMode) || mVideoCodec == VideoCodec::HEVC) {
        addParameter(DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
                             .withDefault(new C2StreamColorAspectsInfo::output(
                                     0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                                     C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                             .withFields({C2F(mColorAspects, range)
                                                  .inRange(C2Color::RANGE_UNSPECIFIED,
                                                           C2Color::RANGE_OTHER),
                                          C2F(mColorAspects, primaries)
                                                  .inRange(C2Color::PRIMARIES_UNSPECIFIED,
                                                           C2Color::PRIMARIES_OTHER),
                                          C2F(mColorAspects, transfer)
                                                  .inRange(C2Color::TRANSFER_UNSPECIFIED,
                                                           C2Color::TRANSFER_OTHER),
                                          C2F(mColorAspects, matrix)
                                                  .inRange(C2Color::MATRIX_UNSPECIFIED,
                                                           C2Color::MATRIX_OTHER)})
                             .withSetter(MergedColorAspectsSetter, mDefaultColorAspects,
                                         mCodedColorAspects)
                             .build());
    }

    std::vector<uint32_t> pixelFormats = {
            static_cast<uint32_t>(HalPixelFormat::YCBCR_420_888),
            static_cast<uint32_t>(HalPixelFormat::IMPLEMENTATION_DEFINED),
    };
    if (has10BitProfile) {
        pixelFormats.push_back(static_cast<uint32_t>(HalPixelFormat::YCBCR_P010));
    }
    addParameter(DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                         .withDefault(new C2StreamPixelFormatInfo::output(
                                 0u, static_cast<uint32_t>(HalPixelFormat::YCBCR_420_888)))
                         .withFields({C2F(mPixelFormat, value).oneOf(pixelFormats)})
                         .withSetter(Setter<decltype(*mPixelFormat)>::StrictValueWithNoDeps)
                         .build());
}

size_t DecodeInterface::getInputBufferSize() const {
    return mMaxInputSize->value;
}

c2_status_t DecodeInterface::queryColorAspects(
        std::shared_ptr<C2StreamColorAspectsInfo::output>* targetColorAspects) {
    std::unique_ptr<C2StreamColorAspectsInfo::output> colorAspects =
            std::make_unique<C2StreamColorAspectsInfo::output>(
                    0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                    C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED);
    c2_status_t status = query({colorAspects.get()}, {}, C2_DONT_BLOCK, nullptr);
    if (status == C2_OK) {
        *targetColorAspects = std::move(colorAspects);
    }
    return status;
}

uint32_t DecodeInterface::getOutputDelay(VideoCodec codec) {
    switch (codec) {
    case VideoCodec::H264:

        return 16;
    case VideoCodec::HEVC:
        return 16;
    case VideoCodec::VP8:

        return 3;
    case VideoCodec::VP9:

        return 8;
    case VideoCodec::AV1:

        return 8;
    case VideoCodec::MPEG4:
        return 4;
    case VideoCodec::H263:
        return 2;
    }
}

}
