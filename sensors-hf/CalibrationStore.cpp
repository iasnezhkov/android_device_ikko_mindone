/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone.sensors.cali"

#include "CalibrationStore.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <limits>

#include <log/log.h>

#include "NvcfgJson.h"

#ifdef MINDONE_HF_RUST_WIRE
#include "HfRustCore.h"
#endif

namespace mindone {
namespace hf {

namespace {

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}


bool allZero(const std::vector<int32_t>& v) {
    for (int32_t x : v) {
        if (x != 0) return false;
    }
    return true;
}

#ifndef MINDONE_HF_RUST_WIRE
void writeRegion(std::vector<uint8_t>* buf, CalibrationStore::RegionLayout region,
                  const std::vector<int32_t>& values, const char* what, uint8_t sensorType) {
    const size_t wantCount = region.length / 4;
    if (values.size() != wantCount) {
        ALOGW("sensor_type=%u: %s region wants %zu int32 value(s), json had %zu - "
              "copying min(both), rest stays zero",
              sensorType, what, wantCount, values.size());
    }
    const size_t n = std::min(wantCount, values.size());
    if (n > 0) {
        std::memcpy(buf->data() + region.offset, values.data(), n * sizeof(int32_t));
    }
}
#endif

}

const std::array<CalibrationStore::SensorLayout, 4>& CalibrationStore::allLayouts() {
    static const std::array<SensorLayout, 4> kLayouts = {{
            {SENSOR_TYPE_ACCELEROMETER, 24, {0, 12}, {12, 12}, {0, 0}, "acc", "acc_bias.json",
             "acc_bias", "acc_cali.json", "acc_cali"},
            {SENSOR_TYPE_MAGNETIC_FIELD, 36, {0, 12}, {12, 24}, {0, 0}, "mag", "mag_bias.json",
             "mag_bias", "mag_cali.json", "mag_cali"},
            {SENSOR_TYPE_GYROSCOPE, 48, {0, 12}, {12, 12}, {24, 24}, "gyro", "gyro_bias.json",
             "gyro_bias", "gyro_cali.json", "gyro_cali"},
            {SENSOR_TYPE_LIGHT, 4, {0, 0}, {0, 4}, {0, 0}, "light", nullptr, nullptr,
             "als_cali.json", "als_cali"},
    }};
    return kLayouts;
}

const CalibrationStore::SensorLayout* CalibrationStore::layoutFor(uint8_t sensorType) {
    for (const auto& layout : allLayouts()) {
        if (layout.sensorType == sensorType) return &layout;
    }
    return nullptr;
}

CalibrationStore::CalibrationStore(std::string baseDir)
    : baseDir_(std::move(baseDir)), writer_(&CalibrationStore::persistenceLoop, this) {}

CalibrationStore::~CalibrationStore() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        changed_.notify_one();
    }
    writer_.join();
}

void CalibrationStore::seedCache(CachedRegion* cache, const std::vector<int32_t>& values) {
    if (cache->haveValue && (!cache->havePersistedValue ||
                            cache->values != cache->persistedValues)) return;
    cache->values = values;
    cache->persistedValues = values;
    cache->haveValue = true;
    cache->havePersistedValue = true;
}

std::vector<uint8_t> CalibrationStore::buildPayload(const SensorLayout& layout) {
#ifdef MINDONE_HF_RUST_WIRE
    std::optional<std::vector<int32_t>> bias;
    std::optional<std::vector<int32_t>> cali;
    std::optional<std::vector<int32_t>> temp;
    if (layout.bias.length > 0 && layout.biasFile) {
        bias = nvcfg_json::readIntArray(baseDir_ + "/" + layout.biasFile, layout.biasKey);
    }
    if (layout.cali.length > 0 && layout.caliFile) {
        bool keyMismatch = false;
        std::string actualKey;
        cali = nvcfg_json::readIntArray(baseDir_ + "/" + layout.caliFile, layout.caliKey,
                                      &keyMismatch, &actualKey);
        if (cali && keyMismatch) {
            ALOGW("sensor_type=%u (%s): calibration key is %s, expected %s; using values",
                  layout.sensorType, layout.name, actualKey.c_str(), layout.caliKey);
        }
    }
    if (layout.temp.length > 0) {
        const std::string key = std::string(layout.name) + "_temp";
        temp = nvcfg_json::readIntArray(baseDir_ + "/" + key + ".json", key);
    }
    struct mindone_hf_calibration_payload result{};
    const int32_t status = mindone_hf_calibration_payload(layout.sensorType,
            bias ? bias->data() : nullptr, bias ? bias->size() : 0,
            cali ? cali->data() : nullptr, cali ? cali->size() : 0,
            temp ? temp->data() : nullptr, temp ? temp->size() : 0, &result);
    const auto& actual = result.layout;
    if (status != 0 || actual.length != layout.totalLength ||
        actual.bias_offset != layout.bias.offset || actual.bias_length != layout.bias.length ||
        actual.cali_offset != layout.cali.offset || actual.cali_length != layout.cali.length ||
        actual.temp_offset != layout.temp.offset || actual.temp_length != layout.temp.length ||
        result.combined > 1 ||
        (result.combined != 0 && (!cali || actual.bias_length == 0 ||
         cali->size() != (actual.bias_length + actual.cali_length) / sizeof(int32_t)))) {
        ALOGE("sensor_type=%u: rejected calibration payload status=%d", layout.sensorType, status);
        return {};
    }
    if (bias) seedCache(&biasCache_[layout.sensorType], *bias);
    if (cali) {
        if (result.combined != 0) {
            const size_t biasCount = actual.bias_length / sizeof(int32_t);
            std::vector<int32_t> biasPart(cali->begin(), cali->begin() + biasCount);
            std::vector<int32_t> caliPart(cali->begin() + biasCount, cali->end());
            seedCache(&biasCache_[layout.sensorType], biasPart);
            seedCache(&caliCache_[layout.sensorType], caliPart);
        } else {
            seedCache(&caliCache_[layout.sensorType], *cali);
        }
    }
    if (temp) seedCache(&tempCache_[layout.sensorType], *temp);
    return std::vector<uint8_t>(result.bytes, result.bytes + actual.length);
#else
    std::vector<uint8_t> buf(layout.totalLength, 0);

    if (layout.bias.length > 0 && layout.biasFile) {
        auto bias = nvcfg_json::readIntArray(baseDir_ + "/" + layout.biasFile, layout.biasKey);
        if (bias) {
            writeRegion(&buf, layout.bias, *bias, "bias", layout.sensorType);
            seedCache(&biasCache_[layout.sensorType], *bias);
        } else {
            ALOGI("sensor_type=%u (%s): no %s - bias region defaults to zero", layout.sensorType,
                  layout.name, layout.biasFile);
        }
    }

    if (layout.cali.length > 0 && layout.caliFile) {
        bool keyMismatch = false;
        std::string actualKey;
        auto cali = nvcfg_json::readIntArray(baseDir_ + "/" + layout.caliFile, layout.caliKey,
                                              &keyMismatch, &actualKey);
        if (!cali) {
            ALOGI("sensor_type=%u (%s): no %s - cali region defaults to zero", layout.sensorType,
                  layout.name, layout.caliFile);
        } else {
            if (keyMismatch) {
                ALOGW("sensor_type=%u (%s): %s key is \"%s\", expected \"%s\" - using values "
                      "anyway",
                      layout.sensorType, layout.name, layout.caliFile, actualKey.c_str(),
                      layout.caliKey);
            }
            const size_t biasCount = layout.bias.length / 4;
            const size_t caliCount = layout.cali.length / 4;
            if (layout.bias.length > 0 && cali->size() == biasCount + caliCount) {
                ALOGI("sensor_type=%u (%s): %s has %zu values (bias+cali count) - "
                      "treating as a combined file, first %zu -> bias, rest -> cali",
                      layout.sensorType, layout.name, layout.caliFile, cali->size(), biasCount);
                std::vector<int32_t> biasPart(cali->begin(), cali->begin() + biasCount);
                std::vector<int32_t> caliPart(cali->begin() + biasCount, cali->end());
                writeRegion(&buf, layout.bias, biasPart, "bias(from combined cali file)",
                            layout.sensorType);
                writeRegion(&buf, layout.cali, caliPart, "cali", layout.sensorType);
                seedCache(&biasCache_[layout.sensorType], biasPart);
                seedCache(&caliCache_[layout.sensorType], caliPart);
            } else {
                writeRegion(&buf, layout.cali, *cali, "cali", layout.sensorType);
                seedCache(&caliCache_[layout.sensorType], *cali);
            }
        }
    }

    if (layout.temp.length > 0) {
        const std::string tempFile = std::string(layout.name) + "_temp.json";
        auto temp = nvcfg_json::readIntArray(baseDir_ + "/" + tempFile,
                                              std::string(layout.name) + "_temp");
        if (temp) {
            writeRegion(&buf, layout.temp, *temp, "temp", layout.sensorType);
            seedCache(&tempCache_[layout.sensorType], *temp);
        }
    }

    return buf;
#endif
}

void CalibrationStore::pushAll(HfManagerClient& client) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& layout : allLayouts()) {
        if (!client.queryRegistered(layout.sensorType)) {
            continue;
        }
        std::vector<uint8_t> payload = buildPayload(layout);
        if (payload.size() != layout.totalLength) continue;
        if (!client.configCalibration(layout.sensorType, payload.data(),
                                       static_cast<uint8_t>(payload.size()))) {
            ALOGE("sensor_type=%u (%s): CONFIG_CALI push failed, %zu bytes", layout.sensorType,
                  layout.name, payload.size());
            continue;
        }
        ALOGI("sensor_type=%u (%s): pushed %zu-byte CONFIG_CALI", layout.sensorType, layout.name,
              payload.size());

        if (layout.bias.length > 0) {
            if (!client.requestBiasReports(layout.sensorType, true)) {
                ALOGE("sensor_type=%u (%s): failed to subscribe to BIAS_ACTION",
                      layout.sensorType, layout.name);
            }
        }
        if (layout.cali.length > 0) {
            if (!client.requestCaliReports(layout.sensorType, true)) {
                ALOGE("sensor_type=%u (%s): failed to subscribe to CALI_ACTION",
                      layout.sensorType, layout.name);
            }
        }
        if (layout.temp.length > 0) {
            if (!client.requestTempReports(layout.sensorType, true)) {
                ALOGE("sensor_type=%u (%s): failed to subscribe to TEMP_ACTION",
                      layout.sensorType, layout.name);
            }
        }
    }
}

void CalibrationStore::onKernelEvent(const hf_manager_event& ke) {
    const SensorLayout* layout = layoutFor(ke.sensor_type);
    if (!layout) return;

    std::lock_guard<std::mutex> lock(mutex_);
#ifdef MINDONE_HF_RUST_WIRE
    struct mindone_hf_calibration_event event{};
    if (mindone_hf_calibration_event(ke.sensor_type, ke.action, ke.word,
                                    sizeof(ke.word) / sizeof(ke.word[0]), &event) != 0) return;
    Region region;
    size_t expected;
    switch (event.region) {
        case 0: region = Region::Bias; expected = layout->bias.length / 4; break;
        case 1: region = Region::Cali; expected = layout->cali.length / 4; break;
        case 2: region = Region::Temp; expected = layout->temp.length / 4; break;
        default: return;
    }
    if (event.count != expected || expected == 0 || expected > 6) return;
    persistRegion(*layout, region, event.words, event.count);
#else
    if (ke.action == BIAS_ACTION && layout->bias.length > 0) {
        persistRegion(*layout, Region::Bias, ke.word, layout->bias.length / 4);
    } else if (ke.action == CALI_ACTION && layout->cali.length > 0) {
        persistRegion(*layout, Region::Cali, ke.word, layout->cali.length / 4);
    } else if (ke.action == TEMP_ACTION && layout->temp.length > 0) {
        persistRegion(*layout, Region::Temp, ke.word, layout->temp.length / 4);
    }
#endif
}

std::unordered_map<uint8_t, CalibrationStore::CachedRegion>& CalibrationStore::cacheFor(Region region) {
    switch (region) {
        case Region::Bias: return biasCache_;
        case Region::Cali: return caliCache_;
        case Region::Temp: return tempCache_;
    }
    std::abort();
}

void CalibrationStore::persistRegion(const SensorLayout& layout, Region region,
                                      const int32_t* words, size_t count) {
    auto& cache = cacheFor(region)[layout.sensorType];
    const std::vector<int32_t> newValues(words, words + count);
    const bool unchanged = cache.haveValue && cache.values == newValues;
    if (unchanged && (cache.pending || allZero(newValues) ||
                      (cache.havePersistedValue && cache.persistedValues == newValues))) return;
    const bool wasPending = cache.pending;
    cache.values = newValues;
    cache.haveValue = true;
    if (allZero(newValues)) {
        if (!unchanged) {
            ALOGW("sensor_type=%u (%s): %s reported all-zero - not persisted (factory data kept)",
                  layout.sensorType, layout.name,
                  region == Region::Bias ? "bias" : region == Region::Cali ? "cali" : "temp");
        }
        cache.pending = false;
    } else {
        cache.pending = !cache.havePersistedValue || cache.persistedValues != newValues;
    }
    if (cache.pending && nowMs() - cache.lastAttemptMs >= kMinRewriteIntervalMs) {
        writePending(layout, region, cache);
    }
    if (cache.pending != wasPending) changed_.notify_one();
}

void CalibrationStore::writePending(const SensorLayout& layout, Region region,
                                    CachedRegion& cache) {
    const std::string tempKey = std::string(layout.name) + "_temp";
    const std::string tempFile = tempKey + ".json";
    const char* file = region == Region::Bias ? layout.biasFile :
            region == Region::Cali ? layout.caliFile : tempFile.c_str();
    const char* key = region == Region::Bias ? layout.biasKey :
            region == Region::Cali ? layout.caliKey : tempKey.c_str();
    const char* what = region == Region::Bias ? "bias" : region == Region::Cali ? "cali" : "temp";
    cache.pending = false;
    cache.lastAttemptMs = nowMs();
    if (!file || !key) return;
    const std::string path = baseDir_ + "/" + file;
    if (nvcfg_json::writeIntArray(path, key, cache.values)) {
        cache.persistedValues = cache.values;
        cache.havePersistedValue = true;
        ALOGI("sensor_type=%u (%s): persisted runtime %s -> %s", layout.sensorType, layout.name,
              what, path.c_str());
    } else {
        ALOGE("sensor_type=%u (%s): failed to persist runtime %s to %s", layout.sensorType,
              layout.name, what, path.c_str());
    }
}

void CalibrationStore::persistenceLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        int64_t next = std::numeric_limits<int64_t>::max();
        for (const auto& layout : allLayouts()) {
            for (Region region : {Region::Bias, Region::Cali, Region::Temp}) {
                auto& regions = cacheFor(region);
                auto it = regions.find(layout.sensorType);
                if (it == regions.end()) continue;
                auto& cache = it->second;
                if (stopping_) {
                    cache.pending = cache.haveValue && !allZero(cache.values) &&
                            (!cache.havePersistedValue || cache.values != cache.persistedValues);
                }
                if (!cache.pending) continue;
                const int64_t due = cache.lastAttemptMs + kMinRewriteIntervalMs;
                if (stopping_ || nowMs() >= due) {
                    writePending(layout, region, cache);
                } else {
                    next = std::min(next, due);
                }
            }
        }
        if (stopping_) return;
        if (next == std::numeric_limits<int64_t>::max()) {
            changed_.wait(lock);
        } else {
            changed_.wait_for(lock, std::chrono::milliseconds(std::max<int64_t>(0, next - nowMs())));
        }
    }
}

}
}
