/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

constexpr int32_t EX_ILLEGAL_ARGUMENT = -3;
constexpr int32_t EX_SERVICE_SPECIFIC = -8;

namespace ndk {
struct ScopedAStatus {
    int32_t exception;
    int32_t service;
    static ScopedAStatus ok() { return {0, 0}; }
    static ScopedAStatus fromExceptionCode(int32_t code) { return {code, 0}; }
    static ScopedAStatus fromServiceSpecificError(int32_t code) {
        return {EX_SERVICE_SPECIFIC, code};
    }
};
}

struct Command {
    uint8_t sensor;
    bool enabled;
    int64_t period;
    int64_t latency;
};

struct Backend {
    bool fail = false;
    std::vector<Command> calls;
    std::unordered_map<uint8_t, Command> accepted;
    bool setEnable(uint8_t sensor, bool enabled, int64_t period, int64_t latency) {
        Command command{sensor, enabled, period, latency};
        calls.push_back(command);
        if (fail) return false;
        accepted[sensor] = command;
        return true;
    }
};

struct Sensors {
    PRODUCTION_STATE
    std::mutex mutex_;
    std::unordered_map<int32_t, uint32_t> gainByHandle_;
    std::unordered_map<int32_t, SensorState> state_;
    Backend hf_;
    void ensureEnumerated() {}
    ::ndk::ScopedAStatus activate(int32_t in_sensorHandle, bool in_enabled);
    ::ndk::ScopedAStatus batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                             int64_t in_maxReportLatencyNs);
};

PRODUCTION_FUNCTIONS

size_t assertions = 0;

void require(bool condition, const char* label) {
    ++assertions;
    if (!condition) {
        std::cerr << label << '\n';
        std::exit(1);
    }
}

void ok(const ndk::ScopedAStatus& status, const char* label) {
    require(status.exception == 0 && status.service == 0, label);
}

void ioError(const ndk::ScopedAStatus& status, const char* label) {
    require(status.exception == EX_SERVICE_SPECIFIC && status.service == EIO, label);
}

void command(const Command& actual, uint8_t sensor, bool enabled, int64_t period,
             int64_t latency, const char* label) {
    require(actual.sensor == sensor && actual.enabled == enabled && actual.period == period &&
                    actual.latency == latency,
            label);
}

void state(const Sensors::SensorState& actual, bool active, int64_t period, int64_t latency,
           const char* label) {
    require(actual.active == active && actual.samplingPeriodNs == period &&
                    actual.maxReportLatencyNs == latency,
            label);
}

int main() {
    Sensors sensors;
    sensors.gainByHandle_[1] = 1000;
    sensors.gainByHandle_[5] = 100;
    ok(sensors.batch(1, 10000000, 50000000), "inactive batch status");
    require(sensors.hf_.calls.empty(), "inactive batch must not write backend");
    state(sensors.state_.at(1), false, 10000000, 50000000, "inactive batch cache");
    ok(sensors.activate(1, true), "activate cached batch");
    command(sensors.hf_.calls.back(), 1, true, 10000000, 50000000, "activate cached values");
    state(sensors.state_.at(1), true, 10000000, 50000000, "activated cache");

    ok(sensors.batch(1, 20000000, 70000000), "active batch status");
    command(sensors.hf_.calls.back(), 1, true, 20000000, 70000000,
            "active batch must send requested values");
    state(sensors.state_.at(1), true, 20000000, 70000000, "active batch accepted cache");
    sensors.hf_.fail = true;
    ioError(sensors.batch(1, 30000000, 90000000), "active batch failure status");
    command(sensors.hf_.calls.back(), 1, true, 30000000, 90000000,
            "failed active batch must send requested values");
    state(sensors.state_.at(1), true, 20000000, 70000000,
            "failed active batch must preserve accepted cache");
    command(sensors.hf_.accepted.at(1), 1, true, 20000000, 70000000,
            "failed active batch backend unchanged");
    ioError(sensors.activate(1, false), "failed disable status");
    state(sensors.state_.at(1), true, 20000000, 70000000, "failed disable keeps state");
    sensors.hf_.fail = false;
    ok(sensors.activate(1, false), "disable after failed batch");
    command(sensors.hf_.calls.back(), 1, false, 20000000, 70000000,
            "disable must use accepted values after failed batch");
    ok(sensors.activate(1, true), "reactivate after failed batch");
    command(sensors.hf_.calls.back(), 1, true, 20000000, 70000000,
            "reactivate must use accepted values after failed batch");
    for (int i = 0; i < 3; ++i) {
        const size_t count = sensors.hf_.calls.size();
        ok(sensors.batch(1, 20000000, 70000000), "idempotent active batch status");
        require(sensors.hf_.calls.size() == count + 1, "active update retains backend call");
        state(sensors.state_.at(1), true, 20000000, 70000000, "idempotent active state");
    }
    sensors.hf_.fail = true;
    ioError(sensors.batch(1, 20000000, 70000000), "idempotent backend failure status");
    state(sensors.state_.at(1), true, 20000000, 70000000, "idempotent failure state");
    const size_t beforeInactive = sensors.hf_.calls.size();
    ok(sensors.batch(5, 0, 0), "inactive one-shot zero timing status");
    ok(sensors.batch(5, 0, 0), "idempotent inactive batch status");
    require(sensors.hf_.calls.size() == beforeInactive, "disabled batch ignores backend failure");
    state(sensors.state_.at(5), false, 0, 0, "disabled batch cache");
    ioError(sensors.activate(5, true), "first activation failure status");
    state(sensors.state_.at(5), false, 0, 0, "first activation failure keeps inactive state");

    const size_t beforeInvalid = sensors.hf_.calls.size();
    const size_t states = sensors.state_.size();
    for (const int32_t handle : {-1, 0, 2, INT32_MAX}) {
        const auto status = sensors.batch(handle, 1, 2);
        require(status.exception == EX_ILLEGAL_ARGUMENT && status.service == 0,
                "invalid handle status");
    }
    require(sensors.hf_.calls.size() == beforeInvalid && sensors.state_.size() == states,
            "invalid handles cannot write backend or cache");
    state(sensors.state_.at(1), true, 20000000, 70000000, "other handle stays unchanged");

    for (int64_t i = 1; i <= 10000; ++i) {
        const int32_t handle = i % 2 ? 1 : 5;
        auto& prior = sensors.state_.at(handle);
        prior.active = i % 3 != 0;
        const bool active = prior.active;
        const int64_t period = i * 1000000;
        const int64_t latency = i * 7000000;
        const int64_t oldPeriod = prior.samplingPeriodNs;
        const int64_t oldLatency = prior.maxReportLatencyNs;
        const size_t count = sensors.hf_.calls.size();
        sensors.hf_.fail = i % 5 == 0;
        const bool rejected = active && sensors.hf_.fail;
        const auto status = sensors.batch(handle, period, latency);
        if (rejected) ioError(status, "replay failure status");
        else ok(status, "replay success status");
        require(sensors.hf_.calls.size() == count + (active ? 1 : 0), "replay backend call count");
        if (active) {
            command(sensors.hf_.calls.back(), static_cast<uint8_t>(handle), true, period, latency,
                    "replay requested values");
        }
        state(sensors.state_.at(handle), active, rejected ? oldPeriod : period,
                rejected ? oldLatency : latency, "replay transactional cache");
        sensors.hf_.fail = false;
        ok(sensors.activate(handle, true), "replay subsequent activation");
        command(sensors.hf_.calls.back(), static_cast<uint8_t>(handle), true,
                rejected ? oldPeriod : period, rejected ? oldLatency : latency,
                "replay activation uses accepted batch");
    }
    std::cout << assertions << " assertions; 10000 transactional batch replays PASS\n";
}
