// Copyright 2022 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "NalParser"

#include <v4l2_codec2/common/HEVCNalParser.h>

#include <algorithm>

#include <media/stagefright/foundation/ABitReader.h>
#include <utils/Log.h>

namespace android {

namespace {

constexpr uint32_t kMaxShortTermRefPicSets = 64;

struct StRefPicSet {

    int numNegativePics;
    int numPositivePics;
    int deltaPocS0[kMaxShortTermRefPicSets];
    int deltaPocS1[kMaxShortTermRefPicSets];

    int numDeltaPocs;
};

bool skipProfileTierLevel(ABitReader* br, uint32_t spsMaxSublayersMinus1) {

    br->skipBits(96);
    if (spsMaxSublayersMinus1 > 6) return false;
    uint32_t subLayerProfilePresentFlag[6];
    uint32_t subLayerLevelPresentFlag[6];
    for (uint32_t i = 0; i < spsMaxSublayersMinus1; ++i) {
        if (!br->getBitsGraceful(1, &subLayerProfilePresentFlag[i])) return false;
        if (!br->getBitsGraceful(1, &subLayerLevelPresentFlag[i])) return false;
    }
    if (spsMaxSublayersMinus1 > 0) {
        br->skipBits(2 * (8 - spsMaxSublayersMinus1));
    }
    for (uint32_t i = 0; i < spsMaxSublayersMinus1; ++i) {
        if (subLayerProfilePresentFlag[i]) {

            br->skipBits(88);
        }
        if (subLayerLevelPresentFlag[i]) {
            br->skipBits(8);
        }
    }
    return true;
}

bool skipScalingListData(ABitReader* br) {
    for (int sizeId = 0; sizeId < 4; ++sizeId) {
        for (int matrixId = 0; matrixId < 6; matrixId += (sizeId == 3) ? 3 : 1) {
            uint32_t scalingListPredModeFlag;
            if (!br->getBitsGraceful(1, &scalingListPredModeFlag)) return false;
            if (!scalingListPredModeFlag) {
                uint32_t unused;
                NalParser::parseUE(br, &unused);
            } else {
                int32_t unused;
                if (sizeId > 1)
                    NalParser::parseSE(br, &unused);
                int coefNum = std::min(64, (1 << (4 + (sizeId << 1))));
                for (int i = 0; i < coefNum; ++i)
                    NalParser::parseSE(br, &unused);
            }
        }
    }
    return true;
}

bool skipStRefPicSet(ABitReader* br, uint32_t stRpsIdx, uint32_t numShortTermRefPicSets,
                     StRefPicSet* allRefPicSets, StRefPicSet* currRefPicSet) {
    uint32_t interRefPicSetPredictionFlag = 0;
    if (stRpsIdx != 0) {
        if (!br->getBitsGraceful(1, &interRefPicSetPredictionFlag)) return false;
    }
    if (interRefPicSetPredictionFlag) {
        uint32_t deltaIdxMinus1 = 0;
        if (stRpsIdx == numShortTermRefPicSets) {
            if (!NalParser::parseUE(br, &deltaIdxMinus1)) return false;
            if (deltaIdxMinus1 + 1 > stRpsIdx) {
                ALOGW("deltaIdxMinus1 is out of range");
                return false;
            }
        }
        int refRpsIdx = stRpsIdx - (static_cast<int>(deltaIdxMinus1) + 1);
        uint32_t deltaRpsSign;
        uint32_t absDeltaRpsMinus1;
        if (!br->getBitsGraceful(1, &deltaRpsSign)) return false;
        if (!NalParser::parseUE(br, &absDeltaRpsMinus1)) return false;
        int deltaRps = (1 - 2 * static_cast<int>(deltaRpsSign)) *
                       (static_cast<int>(absDeltaRpsMinus1) + 1);
        const StRefPicSet& refSet = allRefPicSets[refRpsIdx];
        uint32_t useDeltaFlag[kMaxShortTermRefPicSets];

        std::fill_n(useDeltaFlag, kMaxShortTermRefPicSets, 1);

        for (int j = 0; j <= refSet.numDeltaPocs; j++) {
            uint32_t usedByCurrPicFlag;
            if (!br->getBitsGraceful(1, &usedByCurrPicFlag)) return false;
            if (!usedByCurrPicFlag)
                if (!br->getBitsGraceful(1, &useDeltaFlag[j])) return false;
        }
        int i = 0;
        for (int j = refSet.numPositivePics - 1; j >= 0; --j) {
            int dPoc = refSet.deltaPocS1[j] + deltaRps;
            if (dPoc < 0 && useDeltaFlag[refSet.numNegativePics + j])
                currRefPicSet->deltaPocS0[i++] = dPoc;
        }
        if (deltaRps < 0 && useDeltaFlag[refSet.numDeltaPocs]) {
            currRefPicSet->deltaPocS0[i++] = deltaRps;
        }
        for (int j = 0; j < refSet.numNegativePics; ++j) {
            int dPoc = refSet.deltaPocS0[j] + deltaRps;
            if (dPoc < 0 && useDeltaFlag[j]) currRefPicSet->deltaPocS0[i++] = dPoc;
        }
        currRefPicSet->numNegativePics = i;
        i = 0;
        for (int j = refSet.numNegativePics - 1; j >= 0; --j) {
            int dPoc = refSet.deltaPocS0[j] + deltaRps;
            if (dPoc > 0 && useDeltaFlag[j]) currRefPicSet->deltaPocS1[i++] = dPoc;
        }
        if (deltaRps > 0 && useDeltaFlag[refSet.numDeltaPocs])
            currRefPicSet->deltaPocS1[i++] = deltaRps;
        for (int j = 0; j < refSet.numPositivePics; ++j) {
            int dPoc = refSet.deltaPocS1[j] + deltaRps;
            if (dPoc > 0 && useDeltaFlag[refSet.numNegativePics + j])
                currRefPicSet->deltaPocS1[i++] = dPoc;
        }
        currRefPicSet->numPositivePics = i;
    } else {
        uint32_t uintForRead;
        if (!NalParser::parseUE(br, &uintForRead)) return false;
        currRefPicSet->numNegativePics = static_cast<int>(uintForRead);
        if (!NalParser::parseUE(br, &uintForRead)) return false;
        currRefPicSet->numPositivePics = static_cast<int>(uintForRead);
        if (currRefPicSet->numNegativePics > kMaxShortTermRefPicSets ||
            currRefPicSet->numPositivePics > kMaxShortTermRefPicSets) {
            ALOGW("num_negative_pics or num_positive_pics is out of range");
            return false;
        }
        for (int i = 0; i < currRefPicSet->numNegativePics; ++i) {
            uint32_t deltaPocS0Minus1;
            if (!NalParser::parseUE(br, &deltaPocS0Minus1)) return false;
            if (i == 0) {
                currRefPicSet->deltaPocS0[i] = -(static_cast<int>(deltaPocS0Minus1) + 1);
            } else {
                currRefPicSet->deltaPocS0[i] =
                        currRefPicSet->deltaPocS0[i - 1] - (static_cast<int>(deltaPocS0Minus1) + 1);
            }
            br->skipBits(1);
        }
        for (int i = 0; i < currRefPicSet->numPositivePics; ++i) {
            uint32_t deltaPocS1Minus1;
            if (!NalParser::parseUE(br, &deltaPocS1Minus1)) return false;
            if (i == 0) {
                currRefPicSet->deltaPocS1[i] = static_cast<int>(deltaPocS1Minus1) + 1;
            } else {
                currRefPicSet->deltaPocS1[i] =
                        currRefPicSet->deltaPocS1[i - 1] + static_cast<int>(deltaPocS1Minus1) + 1;
            }
            br->skipBits(1);
        }
    }
    currRefPicSet->numDeltaPocs = currRefPicSet->numNegativePics + currRefPicSet->numPositivePics;
    if (currRefPicSet->numDeltaPocs > kMaxShortTermRefPicSets) {
        ALOGW("numDeltaPocs is out of range");
        return false;
    }
    return true;
}

}

HEVCNalParser::HEVCNalParser(const uint8_t* data, size_t length) : NalParser(data, length) {}

bool HEVCNalParser::locateSPS() {
    while (locateNextNal()) {
        if (length() == 0) continue;
        if (type() != kSPSType) continue;
        return true;
    }

    return false;
}

bool HEVCNalParser::locateIDR() {
    while (locateNextNal()) {
        if (length() == 0) continue;
        if (type() != kIDRType) continue;
        return true;
    }

    return false;
}

uint8_t HEVCNalParser::type() const {

    constexpr uint8_t kNALTypeMask = 0x7e;
    return (*mCurrNalDataPos & kNALTypeMask) >> 1;
}

bool HEVCNalParser::findCodedColorAspects(ColorAspects* colorAspects) {
    ALOG_ASSERT(colorAspects);
    ALOG_ASSERT(type() == kSPSType);

    if (length() <= 2) return false;
    NALBitReader br(mCurrNalDataPos + 2, length() - 2);

    br.skipBits(4);
    uint32_t spsMaxSublayersMinus1;
    if (!br.getBitsGraceful(3, &spsMaxSublayersMinus1)) return false;
    br.skipBits(1);

    if (!skipProfileTierLevel(&br, spsMaxSublayersMinus1)) return false;

    uint32_t unused;
    parseUE(&br, &unused);
    uint32_t chromaFormatIdc;
    if (!parseUE(&br, &chromaFormatIdc)) return false;
    if (chromaFormatIdc == 3) br.skipBits(1);
    parseUE(&br, &unused);
    parseUE(&br, &unused);

    uint32_t conformanceWindowFlag;
    if (!br.getBitsGraceful(1, &conformanceWindowFlag)) return false;
    if (conformanceWindowFlag) {
        parseUE(&br, &unused);
        parseUE(&br, &unused);
        parseUE(&br, &unused);
        parseUE(&br, &unused);
    }
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    uint32_t log2MaxPicOrderCntLsbMinus4;
    if (!parseUE(&br, &log2MaxPicOrderCntLsbMinus4)) return false;

    uint32_t spsSubLayerOrderingInfoPresentFlag;
    if (!br.getBitsGraceful(1, &spsSubLayerOrderingInfoPresentFlag)) return false;
    for (uint32_t i = spsSubLayerOrderingInfoPresentFlag ? 0 : spsMaxSublayersMinus1;
         i <= spsMaxSublayersMinus1; ++i) {
        parseUE(&br, &unused);
        parseUE(&br, &unused);
        parseUE(&br, &unused);
    }
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    parseUE(&br, &unused);
    uint32_t scalingListEnabledFlag;
    if (!br.getBitsGraceful(1, &scalingListEnabledFlag)) return false;
    if (scalingListEnabledFlag) {
        uint32_t spsScalingListDataPresentFlag;
        if (!br.getBitsGraceful(1, &spsScalingListDataPresentFlag)) return false;
        if (spsScalingListDataPresentFlag) {
            if (!skipScalingListData(&br)) return false;
        }
    }

    br.skipBits(2);
    uint32_t pcmEnabledFlag;
    if (!br.getBitsGraceful(1, &pcmEnabledFlag)) return false;
    if (pcmEnabledFlag) {

        br.skipBits(8);
        parseUE(&br, &unused);
        parseUE(&br, &unused);
        br.skipBits(1);
    }

    uint32_t numShortTermRefPicSets;
    if (!parseUE(&br, &numShortTermRefPicSets)) return false;
    if (numShortTermRefPicSets > kMaxShortTermRefPicSets) {
        ALOGW("numShortTermRefPicSets out of range");
        return false;
    }
    StRefPicSet allRefPicSets[kMaxShortTermRefPicSets];
    memset(allRefPicSets, 0, sizeof(StRefPicSet) * kMaxShortTermRefPicSets);
    for (uint32_t i = 0; i < numShortTermRefPicSets; ++i) {
        if (!skipStRefPicSet(&br, i, numShortTermRefPicSets, allRefPicSets, &allRefPicSets[i]))
            return false;
    }

    uint32_t longTermRefPicsPresentFlag;
    if (!br.getBitsGraceful(1, &longTermRefPicsPresentFlag)) return false;
    if (longTermRefPicsPresentFlag) {
        uint32_t numLongTermRefPicsSps;
        if (!parseUE(&br, &numLongTermRefPicsSps)) return false;
        for (uint32_t i = 0; i < numLongTermRefPicsSps; ++i) {

            if (!br.getBitsGraceful(log2MaxPicOrderCntLsbMinus4 + 4, &unused)) return false;
            if (!br.getBitsGraceful(1, &unused)) return false;
        }
    }

    br.skipBits(2);
    uint32_t vuiParametersPresentFlag;
    if (!br.getBitsGraceful(1, &vuiParametersPresentFlag)) return false;
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
        if (overscanInfoPresentFlag) br.skipBits(1);
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
