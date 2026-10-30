// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "NalParser"

#include <v4l2_codec2/common/H264NalParser.h>

#include <media/stagefright/foundation/ABitReader.h>
#include <utils/Log.h>

namespace android {

namespace {

enum H264ProfileIDC {
    kProfileIDCAVLC444 = 44,
    kProfileIDScalableBaseline = 83,
    kProfileIDScalableHigh = 86,
    kProfileIDCHigh = 100,
    kProfileIDHigh10 = 110,
    kProfileIDSMultiviewHigh = 118,
    kProfileIDHigh422 = 122,
    kProfileIDStereoHigh = 128,
    kProfileIDHigh444Predictive = 244,
};

constexpr uint32_t kYUV444Idc = 3;

bool skipScalingList(ABitReader* br, size_t scalingListSize) {
    size_t nextScale = 8;
    size_t lastScale = 8;
    for (size_t j = 0; j < scalingListSize; ++j) {
        if (nextScale != 0) {
            int32_t deltaScale;
            if (!NalParser::parseSE(br, &deltaScale)) return false;
            if (deltaScale < -128) {
                ALOGW("delta scale (%d) is below range, capping to -128", deltaScale);
                deltaScale = -128;
            } else if (deltaScale > 127) {
                ALOGW("delta scale (%d) is above range, capping to 127", deltaScale);
                deltaScale = 127;
            }
            nextScale = (lastScale + (deltaScale + 256)) % 256;
        }
        lastScale = (nextScale == 0) ? lastScale : nextScale;
    }
    return true;
}

bool skipScalingMatrix(ABitReader* br, size_t numScalingLists) {
    for (size_t i = 0; i < numScalingLists; ++i) {
        uint32_t seq_scaling_list_present_flag;
        if (!br->getBitsGraceful(1, &seq_scaling_list_present_flag))
            return false;
        if (seq_scaling_list_present_flag) {
            if (i < 6) {
                if (!skipScalingList(br, 16)) return false;
            } else {
                if (!skipScalingList(br, 64)) return false;
            }
        }
    }
    return true;
}

}

H264NalParser::H264NalParser(const uint8_t* data, size_t length) : NalParser(data, length) {}

bool H264NalParser::locateSPS() {
    while (locateNextNal()) {
        if (length() == 0) continue;
        if (type() != kSPSType) continue;
        return true;
    }

    return false;
}

bool H264NalParser::locateIDR() {
    while (locateNextNal()) {
        if (length() == 0) continue;
        if (type() != kIDRType) continue;
        return true;
    }

    return false;
}

uint8_t H264NalParser::type() const {

    constexpr uint8_t kNALTypeMask = 0x1f;
    return *mCurrNalDataPos & kNALTypeMask;
}

bool H264NalParser::findCodedColorAspects(ColorAspects* colorAspects) {
    ALOG_ASSERT(colorAspects);
    ALOG_ASSERT(type() == kSPSType);

    NALBitReader br(mCurrNalDataPos + 1, length() - 1);

    uint32_t unused;
    uint32_t profileIDC;
    if (!br.getBitsGraceful(8, &profileIDC)) return false;
    br.skipBits(16);
    parseUE(&br, &unused);

    if (profileIDC == kProfileIDCHigh || profileIDC == kProfileIDHigh10 ||
        profileIDC == kProfileIDHigh422 || profileIDC == kProfileIDHigh444Predictive ||
        profileIDC == kProfileIDCAVLC444 || profileIDC == kProfileIDScalableBaseline ||
        profileIDC == kProfileIDScalableHigh || profileIDC == kProfileIDSMultiviewHigh ||
        profileIDC == kProfileIDStereoHigh) {
        uint32_t chromaFormatIDC;
        if (!parseUE(&br, &chromaFormatIDC)) return false;
        if (chromaFormatIDC == kYUV444Idc) {
            br.skipBits(1);
        }

        parseUE(&br, &unused);
        parseUE(&br, &unused);
        br.skipBits(1);

        uint32_t seqScalingMatrixPresentFlag;
        if (!br.getBitsGraceful(1, &seqScalingMatrixPresentFlag))
            return false;
        if (seqScalingMatrixPresentFlag) {
            const size_t numScalingLists = (chromaFormatIDC != kYUV444Idc) ? 8 : 12;
            if (!skipScalingMatrix(&br, numScalingLists)) return false;
        }
    }

    parseUE(&br, &unused);
    uint32_t pictureOrderCountType;
    if (!parseUE(&br, &pictureOrderCountType)) return false;
    if (pictureOrderCountType == 0) {
        parseUE(&br, &unused);
    } else if (pictureOrderCountType == 1) {
        br.skipBits(1);
        int32_t unused_i;
        parseSE(&br, &unused_i);
        parseSE(&br, &unused_i);
        uint32_t numReferenceFrames;
        if (!parseUE(&br, &numReferenceFrames))
            return false;
        for (uint32_t i = 0; i < numReferenceFrames; ++i) {
            parseUE(&br, &unused);
        }
    }

    parseUE(&br, &unused);
    br.skipBits(1);
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    uint32_t frameMbsOnlyFlag;
    if (!br.getBitsGraceful(1, &frameMbsOnlyFlag)) return false;
    if (!frameMbsOnlyFlag) {
        br.skipBits(1);
    }
    br.skipBits(1);

    uint32_t frameCroppingFlag;
    if (!br.getBitsGraceful(1, &frameCroppingFlag)) return false;
    if (frameCroppingFlag) {
        parseUE(&br, &unused);
        parseUE(&br, &unused);
        parseUE(&br, &unused);
        parseUE(&br, &unused);
    }

    uint32_t vuiParametersPresentFlag;
    if (!br.getBitsGraceful(1, &vuiParametersPresentFlag))
        return false;
    if (vuiParametersPresentFlag) {
        uint32_t aspectRatioInfoPresentFlag;
        if (!br.getBitsGraceful(1, &aspectRatioInfoPresentFlag))
            return false;
        if (aspectRatioInfoPresentFlag) {
            uint32_t aspectRatioIdc;
            if (!br.getBitsGraceful(8, &aspectRatioIdc)) return false;
            if (aspectRatioIdc == 255) {
                br.skipBits(32);
            }
        }

        uint32_t overscanInfoPresentFlag;
        if (!br.getBitsGraceful(1, &overscanInfoPresentFlag))
            return false;
        if (overscanInfoPresentFlag) {
            br.skipBits(1);
        }
        uint32_t videoSignalTypePresentFlag;
        if (!br.getBitsGraceful(1, &videoSignalTypePresentFlag))
            return false;
        if (videoSignalTypePresentFlag) {
            br.skipBits(3);
            uint32_t videoFullRangeFlag;
            if (!br.getBitsGraceful(1, &videoFullRangeFlag))
                return false;
            colorAspects->fullRange = videoFullRangeFlag;
            uint32_t color_description_present_flag;
            if (!br.getBitsGraceful(1, &color_description_present_flag))
                return false;
            if (color_description_present_flag) {
                if (!br.getBitsGraceful(8, &colorAspects->primaries))
                    return false;
                if (!br.getBitsGraceful(8, &colorAspects->transfer))
                    return false;
                if (!br.getBitsGraceful(8, &colorAspects->coeffs))
                    return false;
                return true;
            }
        }
    }

    return false;
}

}
