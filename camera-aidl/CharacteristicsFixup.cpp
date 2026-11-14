/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "CharacteristicsFixup.h"
#include "HighFps.h"
#include "MetadataEdit.h"

#include <aidl/android/hardware/camera/device/CameraBlob.h>
#include <log/log.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <tuple>

namespace mindone::camera {
namespace {

using StreamKey = std::tuple<int64_t, int64_t, int64_t>;

struct Duration {
    int32_t format;
    int32_t width;
    int32_t height;
    int64_t minFrame;
    int64_t stall;
};

constexpr Duration kMissingDurations[] = {
        {HAL_PIXEL_FORMAT_RAW16, 4096, 3072, 35906000, 35906000},
};

constexpr int32_t kMaximumResolutionFormats[] = {
        HAL_PIXEL_FORMAT_BLOB, HAL_PIXEL_FORMAT_YCbCr_420_888, HAL_PIXEL_FORMAT_RAW16};

constexpr int64_t kMinJpegBufferSize =
        256 * 1024 + sizeof(::aidl::android::hardware::camera::device::CameraBlob);
constexpr size_t kExtraEntries = 16;
constexpr size_t kExtraData = 4096;

struct MaximumResolutionTables {
    int32_t factor = 0;
    std::vector<int32_t> configs;
    std::vector<int64_t> minFrames;
    std::vector<int64_t> stalls;
};

int64_t maxBlobArea(const std::vector<int32_t>& configs) {
    int64_t area = 0;
    for (size_t i = 0; i + 3 < configs.size(); i += 4) {
        if (configs[i] == HAL_PIXEL_FORMAT_BLOB &&
            configs[i + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
            area = std::max(area, int64_t{configs[i + 1]} * configs[i + 2]);
        }
    }
    return area;
}

bool fixStreams(camera_metadata_t* metadata, std::set<StreamKey>* outputs) {
    auto configs = values<int32_t>(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    auto active = values<int32_t>(metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
    if (configs.empty() || configs.size() % 4 || active.size() != 4) return false;
    std::vector<int32_t> kept;
    for (size_t i = 0; i < configs.size(); i += 4) {
        const bool output = configs[i + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
        if (output && (configs[i + 1] > active[2] || configs[i + 2] > active[3])) {
            ALOGI("Dropping upscaled output format %d %dx%d above the %dx%d active array",
                  configs[i], configs[i + 1], configs[i + 2], active[2], active[3]);
            continue;
        }
        kept.insert(kept.end(), configs.begin() + i, configs.begin() + i + 4);
        if (output) outputs->emplace(configs[i], configs[i + 1], configs[i + 2]);
    }
    const int64_t before = maxBlobArea(configs);
    const int64_t after = maxBlobArea(kept);
    if (!store(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, kept.data(),
               kept.size())) {
        return false;
    }
    auto jpegMax = values<int32_t>(metadata, ANDROID_JPEG_MAX_SIZE);
    if (jpegMax.size() != 1) return false;
    if (after <= 0 || after >= before || jpegMax[0] <= kMinJpegBufferSize) return true;
    const int64_t scaled =
            kMinJpegBufferSize + ((jpegMax[0] - kMinJpegBufferSize) * after + before - 1) / before;
    const int32_t value = static_cast<int32_t>(scaled);
    ALOGI("JPEG max size %d -> %d for a %lld px largest BLOB output", jpegMax[0], value,
          static_cast<long long>(after));
    return store(metadata, ANDROID_JPEG_MAX_SIZE, &value, 1);
}

bool fixDurations(camera_metadata_t* metadata, uint32_t tag, const std::set<StreamKey>& outputs,
                  int64_t Duration::*field, bool required) {
    auto table = values<int64_t>(metadata, tag);
    if (table.size() % 4) return false;
    std::vector<int64_t> kept;
    std::set<StreamKey> covered;
    for (size_t i = 0; i < table.size(); i += 4) {
        const StreamKey key{table[i], table[i + 1], table[i + 2]};
        if (!outputs.count(key) || !covered.insert(key).second) continue;
        kept.insert(kept.end(), table.begin() + i, table.begin() + i + 4);
    }
    for (const auto& fix : kMissingDurations) {
        const StreamKey key{fix.format, fix.width, fix.height};
        if (!outputs.count(key) || !covered.insert(key).second) continue;
        kept.insert(kept.end(), {fix.format, fix.width, fix.height, fix.*field});
    }
    if (required) {
        for (const auto& [format, width, height] : outputs) {
            if (!covered.count({format, width, height})) {
                ALOGE("No duration 0x%x for format %lld %lldx%lld", tag,
                      static_cast<long long>(format), static_cast<long long>(width),
                      static_cast<long long>(height));
            }
        }
    }
    return store(metadata, tag, kept.data(), kept.size());
}

std::vector<int32_t> unique(const std::vector<int32_t>& keys) {
    std::vector<int32_t> result;
    std::set<int32_t> seen;
    for (int32_t key : keys) {
        if (seen.insert(key).second) result.push_back(key);
    }
    return result;
}

bool fixKeyLists(camera_metadata_t* metadata) {
    for (uint32_t tag : {ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                         ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                         ANDROID_REQUEST_AVAILABLE_SESSION_KEYS}) {
        auto keys = unique(values<int32_t>(metadata, tag));
        if (!store(metadata, tag, keys.data(), keys.size())) return false;
    }
    std::vector<int32_t> kept;
    camera_metadata_ro_entry_t entry;
    for (int32_t key : unique(values<int32_t>(metadata,
                                              ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS))) {
        if (find_camera_metadata_ro_entry(metadata, static_cast<uint32_t>(key), &entry) == 0) {
            kept.push_back(key);
        } else {
            ALOGI("Dropping characteristics key 0x%x without an entry", key);
        }
    }
    const int32_t readout = ANDROID_SENSOR_READOUT_TIMESTAMP;
    if (std::find(kept.begin(), kept.end(), readout) == kept.end()) kept.push_back(readout);
    return store(metadata, ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, kept.data(),
                 kept.size());
}

int64_t lookupDuration(const std::vector<int64_t>& table, int32_t format, int32_t width,
                       int32_t height) {
    for (size_t i = 0; i + 3 < table.size(); i += 4) {
        if (table[i] == format && table[i + 1] == width && table[i + 2] == height) {
            return table[i + 3];
        }
    }
    return -1;
}

bool findMaximumResolution(const camera_metadata_t* metadata, MaximumResolutionTables* out) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(
                metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION,
                &entry) == 0) {
        ALOGI("The HAL declares its own maximum resolution configurations");
        return false;
    }
    auto configs = values<int32_t>(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    auto active = values<int32_t>(metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
    if (configs.size() % 4 || active.size() != 4 || active[2] <= 0 || active[3] <= 0) {
        return false;
    }
    auto candidate = [](int32_t format) {
        return std::find(std::begin(kMaximumResolutionFormats), std::end(kMaximumResolutionFormats),
                         format) != std::end(kMaximumResolutionFormats);
    };
    int32_t factor = 0;
    for (size_t i = 0; i < configs.size(); i += 4) {
        const int32_t width = configs[i + 1];
        const int32_t height = configs[i + 2];
        if (configs[i + 3] != ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT ||
            !candidate(configs[i]) || width <= active[2] || width % active[2] ||
            int64_t{width} * active[3] != int64_t{height} * active[2]) {
            continue;
        }
        factor = std::max(factor, width / active[2]);
    }
    if (factor < 2) {
        ALOGW("No output above the %dx%d active array, maximum resolution stays off", active[2],
              active[3]);
        return false;
    }
    const int32_t width = active[2] * factor;
    const int32_t height = active[3] * factor;
    auto minFrames = values<int64_t>(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
    auto stalls = values<int64_t>(metadata, ANDROID_SCALER_AVAILABLE_STALL_DURATIONS);
    for (int32_t format : kMaximumResolutionFormats) {
        bool offered = false;
        for (size_t i = 0; i < configs.size(); i += 4) {
            offered |= configs[i] == format && configs[i + 1] == width &&
                        configs[i + 2] == height &&
                        configs[i + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
        }
        if (!offered) continue;
        const int64_t minFrame = lookupDuration(minFrames, format, width, height);
        const int64_t stall = lookupDuration(stalls, format, width, height);
        if (minFrame <= 0 || stall < 0) {
            ALOGW("No HAL durations for format %d %dx%d, left out of maximum resolution", format,
                  width, height);
            continue;
        }
        out->configs.insert(out->configs.end(),
                            {format, width, height,
                             ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT});
        out->minFrames.insert(out->minFrames.end(), {format, width, height, minFrame});
        out->stalls.insert(out->stalls.end(), {format, width, height, stall});
    }
    out->factor = factor;
    return !out->configs.empty();
}

template <class T>
bool appendValue(camera_metadata_t* metadata, uint32_t tag, T value) {
    auto list = values<T>(metadata, tag);
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
    return store(metadata, tag, list.data(), list.size());
}

bool addMaximumResolution(camera_metadata_t* metadata, const MaximumResolutionTables& tables) {
    auto pixelArray = values<int32_t>(metadata, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE);
    auto active = values<int32_t>(metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
    auto preCorrection =
            values<int32_t>(metadata, ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE);
    if (pixelArray.size() != 2 || active.size() != 4 || preCorrection.size() != 4) return false;
    for (auto* list : {&pixelArray, &active, &preCorrection}) {
        for (auto& value : *list) value *= tables.factor;
    }
    const int32_t binning[] = {tables.factor, tables.factor};
    const uint32_t added[] = {
            ANDROID_SENSOR_INFO_BINNING_FACTOR,
            ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE_MAXIMUM_RESOLUTION,
            ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
            ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION,
            ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS_MAXIMUM_RESOLUTION,
            ANDROID_SCALER_AVAILABLE_STALL_DURATIONS_MAXIMUM_RESOLUTION,
    };
    bool ok = store(metadata, ANDROID_SENSOR_INFO_BINNING_FACTOR, binning, 2) &&
              store(metadata, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                    pixelArray.data(), pixelArray.size()) &&
              store(metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                    active.data(), active.size()) &&
              store(metadata,
                    ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE_MAXIMUM_RESOLUTION,
                    preCorrection.data(), preCorrection.size()) &&
              store(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION,
                    tables.configs.data(), tables.configs.size()) &&
              store(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS_MAXIMUM_RESOLUTION,
                    tables.minFrames.data(), tables.minFrames.size()) &&
              store(metadata, ANDROID_SCALER_AVAILABLE_STALL_DURATIONS_MAXIMUM_RESOLUTION,
                    tables.stalls.data(), tables.stalls.size()) &&
              appendValue<uint8_t>(
                      metadata, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) &&
              appendValue<int32_t>(metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                                   ANDROID_SENSOR_PIXEL_MODE) &&
              appendValue<int32_t>(metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                                   ANDROID_SENSOR_PIXEL_MODE);
    for (uint32_t tag : added) {
        ok = ok && appendValue<int32_t>(metadata, ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                                        static_cast<int32_t>(tag));
    }
    if (ok) {
        ALOGI("Maximum resolution %dx%d, binning %dx%d, %zu output formats", active[2], active[3],
              tables.factor, tables.factor, tables.configs.size() / 4);
    }
    return ok;
}

bool fixHighFps(camera_metadata_t* metadata) {
    auto durations = values<int64_t>(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
    auto ranges = values<int32_t>(metadata, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
    auto keys = values<int32_t>(metadata, ANDROID_REQUEST_AVAILABLE_SESSION_KEYS);
    if (durations.size() % 4 || ranges.size() % 2) return false;
    size_t outputs = 0;
    for (size_t i = 0; i < durations.size(); i += 4) {
        if (!highFpsOutput(durations[i], durations[i + 1], durations[i + 2])) continue;
        durations[i + 3] = std::min(durations[i + 3], kHighFpsFrameDuration);
        ++outputs;
    }
    if (!outputs) return true;
    for (int32_t low : {kNormalMaxFps, kHighFps}) {
        bool known = false;
        for (size_t i = 0; i < ranges.size(); i += 2) {
            known = known || (ranges[i] == low && ranges[i + 1] == kHighFps);
        }
        if (!known) ranges.insert(ranges.end(), {low, kHighFps});
    }
    const int32_t fpsKey = ANDROID_CONTROL_AE_TARGET_FPS_RANGE;
    if (std::find(keys.begin(), keys.end(), fpsKey) == keys.end()) keys.push_back(fpsKey);
    ALOGI("hfps60: %zu PRIVATE/YUV outputs get a %lld ns min frame duration", outputs,
          static_cast<long long>(kHighFpsFrameDuration));
    return store(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, durations.data(),
                 durations.size()) &&
           store(metadata, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, ranges.data(),
                 ranges.size()) &&
           store(metadata, ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, keys.data(), keys.size());
}

}

bool fixCharacteristics(std::vector<uint8_t>* bytes, bool maximumResolution) {
    return editMetadata(bytes, kExtraEntries, kExtraData, [&](camera_metadata_t* metadata) {
        MaximumResolutionTables tables;
        const bool addTables = maximumResolution && findMaximumResolution(metadata, &tables);
        std::set<StreamKey> outputs;
        const uint8_t readout = ANDROID_SENSOR_READOUT_TIMESTAMP_NOT_SUPPORTED;
        return fixStreams(metadata, &outputs) &&
               fixDurations(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, outputs,
                            &Duration::minFrame, true) &&
               fixDurations(metadata, ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, outputs,
                            &Duration::stall, false) &&
               store(metadata, ANDROID_SENSOR_READOUT_TIMESTAMP, &readout, 1) &&
               (!addTables || addMaximumResolution(metadata, tables)) && fixKeyLists(metadata) &&
               (!highFpsEnabled() || fixHighFps(metadata));
    });
}

}
