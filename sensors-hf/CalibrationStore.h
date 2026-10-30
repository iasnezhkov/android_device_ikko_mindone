/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINDONE_CALIBRATION_STORE_H_
#define MINDONE_CALIBRATION_STORE_H_

#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "HfManagerClient.h"
#include "hf_manager_uapi.h"

namespace mindone {
namespace hf {

class CalibrationStore {
  public:
    struct RegionLayout {
        uint8_t offset;
        uint8_t length;
    };

    struct SensorLayout {
        uint8_t sensorType;
        uint8_t totalLength;
        RegionLayout bias;
        RegionLayout cali;
        RegionLayout temp;
        const char* name;
        const char* biasFile;
        const char* biasKey;
        const char* caliFile;
        const char* caliKey;
    };

    explicit CalibrationStore(std::string baseDir = "/mnt/vendor/nvcfg/sensor");
    ~CalibrationStore();

    void pushAll(HfManagerClient& client);

    void onKernelEvent(const hf_manager_event& ke);

    static const SensorLayout* layoutFor(uint8_t sensorType);
    static const std::array<SensorLayout, 4>& allLayouts();

  private:
    static constexpr int64_t kMinRewriteIntervalMs = 2000;

    enum class Region { Bias, Cali, Temp };

    struct CachedRegion {
        std::vector<int32_t> values;
        std::vector<int32_t> persistedValues;
        int64_t lastAttemptMs = -kMinRewriteIntervalMs;
        bool haveValue = false;
        bool havePersistedValue = false;
        bool pending = false;
    };

    std::vector<uint8_t> buildPayload(const SensorLayout& layout);
    static void seedCache(CachedRegion* cache, const std::vector<int32_t>& values);
    void persistRegion(const SensorLayout& layout, Region region, const int32_t* words,
                        size_t count);

    void writePending(const SensorLayout& layout, Region region, CachedRegion& cache);
    void persistenceLoop();
    std::unordered_map<uint8_t, CachedRegion>& cacheFor(Region region);

    const std::string baseDir_;
    std::mutex mutex_;
    std::unordered_map<uint8_t, CachedRegion> biasCache_;
    std::unordered_map<uint8_t, CachedRegion> caliCache_;
    std::unordered_map<uint8_t, CachedRegion> tempCache_;
    std::condition_variable changed_;
    bool stopping_ = false;
    std::thread writer_;
};

}
}

#endif
