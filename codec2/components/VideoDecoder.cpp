// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <v4l2_codec2/components/VideoDecoder.h>

namespace android {

const char* VideoDecoder::DecodeStatusToString(VideoDecoder::DecodeStatus status) {
    switch (status) {
    case VideoDecoder::DecodeStatus::kOk:
        return "OK";
    case VideoDecoder::DecodeStatus::kAborted:
        return "ABORTED";
    case VideoDecoder::DecodeStatus::kError:
        return "ERROR";
    }
}

VideoDecoder::~VideoDecoder() = default;

}
