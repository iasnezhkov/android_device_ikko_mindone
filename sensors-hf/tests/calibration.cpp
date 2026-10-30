/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "HfRustCore.h"

static std::atomic<int64_t> fakeNow{10000};
static int testClock(clockid_t, timespec* value) {
    const auto now = fakeNow.load();
    value->tv_sec = now / 1000;
    value->tv_nsec = now % 1000 * 1000000;
    return 0;
}

#ifdef MINDONE_HF_RUST_WIRE
static bool failPayload = false;
static bool failEvent = false;
static bool corruptCombined = false;
static int32_t fixturePayload(uint8_t sensor, const int32_t* bias, size_t biasCount,
        const int32_t* cali, size_t caliCount, const int32_t* temp, size_t tempCount,
        struct mindone_hf_calibration_payload* output) {
    const int32_t result = mindone_hf_calibration_payload(sensor, bias, biasCount,
                                                       cali, caliCount, temp, tempCount, output);
    if (corruptCombined && result == 0) output->combined = 1;
    return failPayload ? -1 : result;
}
static int32_t fixtureEvent(uint8_t sensor, uint8_t action, const int32_t* words,
                            size_t count, struct mindone_hf_calibration_event* output) {
    const int32_t result = mindone_hf_calibration_event(sensor, action, words, count, output);
    return failEvent ? -1 : result;
}
#define mindone_hf_calibration_payload(...) fixturePayload(__VA_ARGS__)
#define mindone_hf_calibration_event(...) fixtureEvent(__VA_ARGS__)
#endif
#define private public
#include "CalibrationStore.h"
#undef private
#define clock_gettime testClock
#include "CalibrationStore.cpp"
#undef clock_gettime
#ifdef MINDONE_HF_RUST_WIRE
#undef mindone_hf_calibration_payload
#undef mindone_hf_calibration_event
#endif

using namespace mindone::hf;
extern "C" void contract_calibration_payload(uint8_t, uint8_t, uint8_t, const int32_t*, uint8_t*);

static int checks = 0;
static int errors = 0;
static int writes = 0;
static bool configFailure = false;
static bool subscriptionFailure = false;
static std::set<uint8_t> registered;
static std::mutex diskMutex;
static std::map<std::string, std::vector<int32_t>> disk;
static std::vector<std::string> calls;
static std::ostringstream trace;

static void check(bool value, const char* name) {
    ++checks;
    if (!value) {
        ++errors;
        if (errors < 30) std::fprintf(stderr, "FAIL %s\n", name);
    }
}

namespace mindone { namespace hf {
HfManagerClient::~HfManagerClient() = default;
bool HfManagerClient::queryRegistered(uint8_t sensor) {
    calls.push_back("query" + std::to_string(sensor));
    return registered.count(sensor) != 0;
}
bool HfManagerClient::configCalibration(uint8_t sensor, const void* data, uint8_t size) {
    calls.push_back("config" + std::to_string(sensor));
    check(data != nullptr && size == CalibrationStore::layoutFor(sensor)->totalLength,
          "backend receives a complete payload");
    return !configFailure;
}
bool HfManagerClient::requestBiasReports(uint8_t sensor, bool enabled) {
    calls.push_back("bias" + std::to_string(sensor));
    return enabled && !subscriptionFailure;
}
bool HfManagerClient::requestCaliReports(uint8_t sensor, bool enabled) {
    calls.push_back("cali" + std::to_string(sensor));
    return enabled && !subscriptionFailure;
}
bool HfManagerClient::requestTempReports(uint8_t sensor, bool enabled) {
    calls.push_back("temp" + std::to_string(sensor));
    return enabled && !subscriptionFailure;
}
namespace nvcfg_json {
std::optional<std::vector<int32_t>> readIntArray(const std::string& path,
        const std::string&, bool* mismatch, std::string* key) {
    std::lock_guard<std::mutex> lock(diskMutex);
    if (mismatch) *mismatch = true;
    if (key) *key = "legacy_key";
    const auto found = disk.find(path);
    return found == disk.end() ? std::nullopt : std::optional(found->second);
}
bool writeIntArray(const std::string& path, const std::string&, const std::vector<int32_t>& values) {
    std::lock_guard<std::mutex> lock(diskMutex);
    ++writes;
    disk[path] = values;
    return true;
}
}
}}

static uint32_t randomState = 17;
static uint32_t randomWord() {
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

static std::vector<int32_t> values(size_t count) {
    std::vector<int32_t> result(count);
    for (auto& value : result) value = static_cast<int32_t>(randomWord());
    return result;
}

static void clearCaches(CalibrationStore& store) {
    store.biasCache_.clear();
    store.caliCache_.clear();
    store.tempCache_.clear();
}

static std::string cacheTrace(CalibrationStore& store) {
    std::ostringstream result;
    for (auto region : {CalibrationStore::Region::Bias, CalibrationStore::Region::Cali,
                        CalibrationStore::Region::Temp}) {
        auto& map = store.cacheFor(region);
        for (const auto& layout : CalibrationStore::allLayouts()) {
            const auto found = map.find(layout.sensorType);
            if (found == map.end()) continue;
            const auto& cache = found->second;
            result << static_cast<int>(region) << ':' << static_cast<int>(layout.sensorType)
                   << ':' << cache.haveValue << cache.havePersistedValue << cache.pending
                   << ':' << cache.lastAttemptMs;
            for (auto value : cache.values) result << ',' << value;
            result << '/';
            for (auto value : cache.persistedValues) result << ',' << value;
            result << ';';
        }
    }
    return result.str();
}

static void contractRegion(const CalibrationStore::SensorLayout& layout, uint8_t action,
                         const std::vector<int32_t>& values, std::array<uint8_t, 48>& output) {
    std::array<int32_t, 16> words{};
    std::copy_n(values.begin(), std::min(values.size(), words.size()), words.begin());
    contract_calibration_payload(layout.sensorType, layout.totalLength, action, words.data(), output.data());
}

static void payloadReplay() {
    CalibrationStore store("/test");
    std::lock_guard<std::mutex> lock(store.mutex_);
    for (int iteration = 0; iteration < 4096; ++iteration) {
        const auto& layout = CalibrationStore::allLayouts()[iteration % 4];
        auto bias = values((randomWord() >> 16) % 16);
        auto cali = values((randomWord() >> 16) % 16);
        auto temp = values((randomWord() >> 16) % 16);
        if (iteration == 4095) cali = values(100000);
        const bool haveBias = layout.bias.length && ((randomWord() >> 16) & 1);
        const bool haveCali = ((randomWord() >> 16) & 1) || iteration == 4095;
        const bool haveTemp = layout.temp.length && ((randomWord() >> 16) & 1);
        {
            std::lock_guard<std::mutex> diskLock(diskMutex);
            disk.clear();
            if (haveBias) disk[std::string("/test/") + layout.biasFile] = bias;
            if (haveCali) disk[std::string("/test/") + layout.caliFile] = cali;
            if (haveTemp) disk[std::string("/test/") + layout.name + "_temp.json"] = temp;
        }
        clearCaches(store);
        const auto actual = store.buildPayload(layout);
        std::array<uint8_t, 48> expected{};
        if (haveBias) contractRegion(layout, BIAS_ACTION, bias, expected);
        if (haveCali) {
            const size_t count = layout.bias.length / 4;
            if (count && cali.size() == count + layout.cali.length / 4) {
                contractRegion(layout, BIAS_ACTION, {cali.begin(), cali.begin() + count}, expected);
                contractRegion(layout, CALI_ACTION, {cali.begin() + count, cali.end()}, expected);
            } else {
                contractRegion(layout, CALI_ACTION, cali, expected);
            }
        }
        if (haveTemp) contractRegion(layout, TEMP_ACTION, temp, expected);
        check(actual == std::vector<uint8_t>(expected.begin(), expected.begin() + layout.totalLength),
              "payload matches independent calibration contract");
        trace << iteration << ':';
        for (auto byte : actual) trace << static_cast<unsigned>(byte) << ',';
        trace << cacheTrace(store) << '\n';
    }
    clearCaches(store);
}

static void seedAndBackend() {
    {
        std::lock_guard<std::mutex> lock(diskMutex);
        disk.clear();
        disk["/test/acc_bias.json"] = {1, 1, 1};
        disk["/test/acc_cali.json"] = {2, 2, 2, 3, 3, 3};
    }
    CalibrationStore store("/test");
    {
        std::lock_guard<std::mutex> lock(store.mutex_);
        const auto& layout = *CalibrationStore::layoutFor(SENSOR_TYPE_ACCELEROMETER);
        store.buildPayload(layout);
        check(store.biasCache_[1].values == std::vector<int32_t>({2, 2, 2}),
              "combined file overrides clean split seed");
        auto& cache = store.biasCache_[1];
        cache.values = {9, 9, 9};
        cache.pending = true;
        cache.lastAttemptMs = fakeNow;
        const auto before = cacheTrace(store);
        store.buildPayload(layout);
        check(cacheTrace(store) == before, "reseed preserves dirty cache and pending deadline");
        cache.havePersistedValue = false;
        const auto failed = cacheTrace(store);
        store.buildPayload(layout);
        check(cacheTrace(store) == failed, "reseed preserves never-persisted live update");
        trace << before << failed << '\n';
        clearCaches(store);
    }
    registered = {4};
    HfManagerClient client;
    configFailure = true;
    calls.clear();
    store.pushAll(client);
    check(calls == std::vector<std::string>({"query1", "query2", "query4", "config4", "query5"}),
          "failed configuration never subscribes");
    for (const auto& call : calls) trace << call << ',';
    configFailure = false;
    subscriptionFailure = true;
    calls.clear();
    store.pushAll(client);
    check(calls == std::vector<std::string>({"query1", "query2", "query4", "config4", "bias4",
                                          "cali4", "temp4", "query5"}),
          "successful configuration attempts all subscriptions in order");
    for (const auto& call : calls) trace << call << ',';
    subscriptionFailure = false;
#ifdef MINDONE_HF_RUST_WIRE
    {
        std::lock_guard<std::mutex> lock(store.mutex_);
        const auto before = cacheTrace(store);
        failPayload = true;
        check(store.buildPayload(*CalibrationStore::layoutFor(1)).empty(), "FFI rejection yields no payload");
        check(cacheTrace(store) == before, "FFI rejection cannot seed partial caches");
    }
    calls.clear();
    store.pushAll(client);
    check(calls == std::vector<std::string>({"query1", "query2", "query4", "query5"}),
          "FFI rejection stops outer configuration and subscriptions");
    failPayload = false;
    for (size_t count : {size_t(1), size_t(7)}) {
        std::lock_guard<std::mutex> lock(store.mutex_);
        {
            std::lock_guard<std::mutex> diskLock(diskMutex);
            disk["/test/acc_cali.json"] = std::vector<int32_t>(count, 1);
        }
        const auto before = cacheTrace(store);
        corruptCombined = true;
        check(store.buildPayload(*CalibrationStore::layoutFor(1)).empty() && cacheTrace(store) == before,
              "invalid combined selector rejected before cache seeding");
        corruptCombined = false;
    }
#endif
}

static void eventMatrix() {
    CalibrationStore store("/test");
    const int before = writes;
    int accepted = 0;
    for (unsigned sensor = 0; sensor < 256; ++sensor) {
        for (unsigned action = 0; action < 256; ++action) {
            fakeNow += 2000;
            hf_manager_event event{};
            event.sensor_type = sensor;
            event.action = action;
            for (int index = 0; index < 16; ++index) event.word[index] = INT32_MIN + index;
            const auto* layout = CalibrationStore::layoutFor(sensor);
            const size_t count = !layout ? 0 : action == BIAS_ACTION ? layout->bias.length / 4 :
                    action == CALI_ACTION ? layout->cali.length / 4 :
                    action == TEMP_ACTION ? layout->temp.length / 4 : 0;
            store.onKernelEvent(event);
            if (count) {
                ++accepted;
                std::lock_guard<std::mutex> lock(store.mutex_);
                auto region = action == BIAS_ACTION ? CalibrationStore::Region::Bias :
                        action == CALI_ACTION ? CalibrationStore::Region::Cali : CalibrationStore::Region::Temp;
                const auto& cache = store.cacheFor(region);
                const auto found = cache.find(sensor);
                check(found != cache.end() && found->second.values == std::vector<int32_t>(event.word, event.word + count),
                      "outer dispatch stores exactly the supported signed words");
                trace << cacheTrace(store) << '\n';
            }
            std::lock_guard<std::mutex> lock(diskMutex);
            check(writes == before + accepted, "unknown or unsupported event has no persistence effect");
        }
    }
#ifdef MINDONE_HF_RUST_WIRE
    hf_manager_event event{};
    event.sensor_type = SENSOR_TYPE_GYROSCOPE;
    event.action = TEMP_ACTION;
    std::fill(std::begin(event.word), std::end(event.word), 19);
    failEvent = true;
    std::string snapshot;
    {
        std::lock_guard<std::mutex> lock(store.mutex_);
        snapshot = cacheTrace(store);
    }
    store.onKernelEvent(event);
    {
        std::lock_guard<std::mutex> lock(store.mutex_);
        check(cacheTrace(store) == snapshot, "event FFI rejection preserves outer cache state");
    }
    failEvent = false;
#endif
}

#ifdef MINDONE_HF_RUST_WIRE
static void ffiBoundary() {
    static_assert(sizeof(struct mindone_hf_calibration_layout) == 28);
    static_assert(sizeof(struct mindone_hf_calibration_payload) == 80);
    static_assert(alignof(struct mindone_hf_calibration_payload) == 4);
    static_assert(offsetof(struct mindone_hf_calibration_payload, layout) == 48);
    static_assert(offsetof(struct mindone_hf_calibration_payload, combined) == 76);
    static_assert(sizeof(struct mindone_hf_calibration_event) == 32);
    static_assert(alignof(struct mindone_hf_calibration_event) == 4);
    static_assert(offsetof(struct mindone_hf_calibration_event, count) == 24);
    static_assert(offsetof(struct mindone_hf_calibration_event, region) == 28);
    struct mindone_hf_calibration_payload output;
    std::memset(&output, 0xa5, sizeof(output));
    const auto before = output;
    alignas(4) std::array<uint8_t, 80> raw{};
    std::array<int32_t, 16> words{};
    for (unsigned sensor = 0; sensor < 256; ++sensor) {
        const auto* layout = CalibrationStore::layoutFor(sensor);
        const int result = mindone_hf_calibration_payload(sensor, nullptr, 0, nullptr, 0, nullptr, 0, &output);
        if (!layout) {
            check(result != 0 && std::memcmp(&output, &before, sizeof(output)) == 0,
                  "unsupported payload leaves output unchanged");
        } else {
            const auto& actual = output.layout;
            check(result == 0 && actual.length == layout->totalLength &&
                  actual.bias_offset == layout->bias.offset && actual.bias_length == layout->bias.length &&
                  actual.cali_offset == layout->cali.offset && actual.cali_length == layout->cali.length &&
                  actual.temp_offset == layout->temp.offset && actual.temp_length == layout->temp.length,
                  "Rust layouts equal every production C++ table entry");
        }
        output = before;
    }
    for (auto count : {size_t(1), SIZE_MAX, SIZE_MAX / 4, SIZE_MAX / 8 + 1}) {
        const auto* input = count == 1 ? nullptr : words.data();
        check(mindone_hf_calibration_payload(4, input, count, nullptr, 0, nullptr, 0, &output) != 0 &&
              std::memcmp(&output, &before, sizeof(output)) == 0, "invalid slice arithmetic preserves output");
    }
    check(mindone_hf_calibration_payload(4, reinterpret_cast<int32_t*>(raw.data() + 1), 1,
          nullptr, 0, nullptr, 0, &output) != 0, "misaligned input rejected");
    check(mindone_hf_calibration_payload(4, reinterpret_cast<int32_t*>(UINTPTR_MAX & ~uintptr_t(3)), 2,
          nullptr, 0, nullptr, 0, &output) != 0, "wrapping address rejected");
    check(mindone_hf_calibration_payload(4, nullptr, 0, nullptr, 0, nullptr, 0, nullptr) != 0,
          "null output rejected");
    check(mindone_hf_calibration_payload(4, nullptr, 0, nullptr, 0, nullptr, 0,
          reinterpret_cast<struct mindone_hf_calibration_payload*>(raw.data() + 1)) != 0,
          "misaligned output rejected");
    struct mindone_hf_calibration_event event;
    std::memset(&event, 0xa5, sizeof(event));
    const auto eventBefore = event;
    for (unsigned sensor = 0; sensor < 256; ++sensor) {
        for (unsigned action = 0; action < 256; ++action) {
            const int result = mindone_hf_calibration_event(sensor, action, words.data(), words.size(), &event);
            if (result != 0) check(std::memcmp(&event, &eventBefore, sizeof(event)) == 0,
                                  "unsupported event leaves output unchanged");
            event = eventBefore;
        }
    }
    for (size_t count = 0; count < 18; ++count) {
        if (count == 16) continue;
        check(mindone_hf_calibration_event(4, TEMP_ACTION, words.data(), count, &event) != 0 &&
              std::memcmp(&event, &eventBefore, sizeof(event)) == 0, "partial or extra event words rejected");
    }
    check(mindone_hf_calibration_event(4, TEMP_ACTION, nullptr, 16, &event) != 0,
          "null event words rejected");
    check(mindone_hf_calibration_event(4, TEMP_ACTION, reinterpret_cast<int32_t*>(raw.data() + 1), 16, &event) != 0,
          "misaligned event words rejected");
}
#endif

int main(int argc, char**) {
    payloadReplay();
    seedAndBackend();
    eventMatrix();
#ifdef MINDONE_HF_RUST_WIRE
    ffiBoundary();
#endif
    if (argc > 1) std::printf("%s", trace.str().c_str());
    else std::printf("checks=%d failures=%d payload-replays=4096 event-pairs=65536\n", checks, errors);
    return errors ? 1 : 0;
}
