/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "HfRustCore.h"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

static_assert(sizeof(mindone_hf_state) == 24 && alignof(mindone_hf_state) == 8);
static_assert(offsetof(mindone_hf_state, period_ns) == 0);
static_assert(offsetof(mindone_hf_state, latency_ns) == 8);
static_assert(offsetof(mindone_hf_state, active) == 16);
static_assert(offsetof(mindone_hf_state, reserved) == 20);
static_assert(sizeof(mindone_hf_state_transition) == 32 && alignof(mindone_hf_state_transition) == 8);
static_assert(offsetof(mindone_hf_state_transition, command) == 24);
static_assert(offsetof(mindone_hf_state_transition, reserved) == 28);

constexpr int32_t EX_ILLEGAL_ARGUMENT = -3;
constexpr int32_t EX_SERVICE_SPECIFIC = -8;

namespace ndk {
struct ScopedAStatus {
    int32_t exception;
    int32_t service;
    static ScopedAStatus ok() { return {0, 0}; }
    static ScopedAStatus fromExceptionCode(int32_t code) { return {code, 0}; }
    static ScopedAStatus fromServiceSpecificError(int32_t code) { return {EX_SERVICE_SPECIFIC, code}; }
};
}

std::atomic<size_t> assertions{0};
void require(bool condition, const char* label) {
    ++assertions;
    if (!condition) {
        std::cerr << label << '\n';
        std::exit(1);
    }
}

struct Command {
    int32_t sensor;
    int32_t operation;
    int64_t period;
    int64_t latency;
};

struct Backend {
    bool fail = false;
    std::vector<Command> calls;
    std::function<void()> hook;
    bool setEnable(uint8_t sensor, bool enabled, int64_t period, int64_t latency) {
        calls.push_back({sensor, enabled ? 1 : 0, period, latency});
        if (hook) hook();
        return !fail;
    }
    bool requestFlush(uint8_t sensor) {
        calls.push_back({sensor, 2, 0, 0});
        if (hook) hook();
        return !fail;
    }
};

struct SensorInfo {
    static constexpr int32_t SENSOR_FLAG_BITS_MASK_REPORTING_MODE = 14;
    static constexpr int32_t SENSOR_FLAG_BITS_ONE_SHOT_MODE = 4;
    int32_t sensorHandle;
    int32_t flags;
};

struct Sensors {
    PRODUCTION_STATE
    std::mutex mutex_;
    std::unordered_map<int32_t, uint32_t> gainByHandle_{{1, 1000}, {5, 100}};
    std::unordered_map<int32_t, SensorState> state_;
    std::vector<SensorInfo> sensorList_{{1, 0}, {5, 4}};
    Backend hf_;
    void ensureEnumerated() {}
    ndk::ScopedAStatus activate(int32_t, bool);
    ndk::ScopedAStatus batch(int32_t, int64_t, int64_t);
    ndk::ScopedAStatus flush(int32_t);
    SensorState snapshot(int32_t handle) const {
        const auto found = state_.find(handle);
        return found == state_.end() ? SensorState{} : found->second;
    }
};

PRODUCTION_FUNCTIONS

bool same(const Sensors::SensorState& a, const Sensors::SensorState& b) {
    return a.active == b.active && a.samplingPeriodNs == b.samplingPeriodNs &&
            a.maxReportLatencyNs == b.maxReportLatencyNs;
}

void checkStatus(ndk::ScopedAStatus status, int32_t exception, int32_t service, const char* label) {
    require(status.exception == exception && status.service == service, label);
}

void replay(bool trace) {
    Sensors sensors;
    for (int64_t index = 0; index < 10000; ++index) {
        const int32_t handle = index % 5 == 0 ? 5 : 1;
        const auto prior = sensors.snapshot(handle);
        const size_t count = sensors.hf_.calls.size();
        sensors.hf_.fail = index % 7 == 0;
        const int operation = (index / 2) % 4;
        bool backend = false;
        bool invalid = false;
        auto expected = prior;
        ndk::ScopedAStatus status;
        if (operation < 2) {
            status = sensors.activate(handle, operation != 0);
            backend = true;
            expected.active = operation != 0;
        } else if (operation == 2) {
            status = sensors.batch(handle, index * 1000000, index * 7000000);
            backend = prior.active;
            expected.samplingPeriodNs = index * 1000000;
            expected.maxReportLatencyNs = index * 7000000;
        } else {
            status = sensors.flush(handle);
            invalid = !prior.active || handle == 5;
            backend = !invalid;
        }
        const bool failed = backend && sensors.hf_.fail;
        if (invalid) checkStatus(status, EX_ILLEGAL_ARGUMENT, 0, "inactive or one-shot flush rejected");
        else if (failed) checkStatus(status, EX_SERVICE_SPECIFIC, EIO, "backend failure status");
        else checkStatus(status, 0, 0, "successful state operation");
        require(sensors.hf_.calls.size() == count + (backend ? 1 : 0), "expected backend action count");
        const auto actual = sensors.snapshot(handle);
        require(same(actual, invalid || failed ? prior : expected), "transaction commits only after backend success");
        if (backend) {
            const auto& command = sensors.hf_.calls.back();
            require(command.sensor == handle, "backend sensor identity");
            require(command.operation == (operation < 2 ? operation : operation == 2 ? 1 : 2), "backend action identity");
            if (operation != 3) {
                require(command.period == expected.samplingPeriodNs && command.latency == expected.maxReportLatencyNs,
                        "backend receives requested timing");
            }
        }
        if (trace) {
            std::cout << status.exception << ':' << status.service << ':' << actual.active << ':'
                      << actual.samplingPeriodNs << ':' << actual.maxReportLatencyNs << ':' << backend;
            if (backend) {
                const auto& command = sensors.hf_.calls.back();
                std::cout << ':' << command.sensor << ':' << command.operation << ':' << command.period << ':' << command.latency;
            }
            std::cout << '\n';
        }
    }
}

void lifecycle() {
    Sensors sensors;
    for (int handle : {-1, 0, 2, INT32_MAX}) {
        checkStatus(sensors.activate(handle, true), EX_ILLEGAL_ARGUMENT, 0, "unknown activation rejected");
        checkStatus(sensors.batch(handle, 0, 0), EX_ILLEGAL_ARGUMENT, 0, "unknown batch rejected");
        checkStatus(sensors.flush(handle), EX_ILLEGAL_ARGUMENT, 0, "unknown flush rejected");
    }
    require(sensors.state_.empty() && sensors.hf_.calls.empty(), "unknown handles leave no side effects");
    checkStatus(sensors.batch(1, INT64_MAX, INT64_MAX), 0, 0, "maximum nanoseconds accepted exactly");
    checkStatus(sensors.activate(1, true), 0, 0, "activate maximum timing");
    require(sensors.hf_.calls.back().period == INT64_MAX && sensors.hf_.calls.back().latency == INT64_MAX,
            "maximum nanoseconds do not overflow");
    sensors.hf_.hook = [&] {
        const bool unlocked = std::async(std::launch::async, [&] {
            if (!sensors.mutex_.try_lock()) return false;
            sensors.mutex_.unlock();
            return true;
        }).get();
        require(!unlocked, "backend action remains under lifecycle mutex");
    };
    checkStatus(sensors.batch(1, 3, 4), 0, 0, "locked batch");
    checkStatus(sensors.flush(1), 0, 0, "locked flush");
    checkStatus(sensors.activate(1, false), 0, 0, "locked deactivation");
    sensors.hf_.hook = {};
    checkStatus(sensors.activate(5, true), 0, 0, "one-shot activate");
    checkStatus(sensors.flush(5), EX_ILLEGAL_ARGUMENT, 0, "one-shot flush rejects backend");
    checkStatus(sensors.activate(5, false), 0, 0, "one-shot deactivate");
    checkStatus(sensors.activate(5, true), 0, 0, "one-shot explicit re-arm issues enable");
    require(sensors.hf_.calls.back().sensor == 5 && sensors.hf_.calls.back().operation == 1,
            "one-shot re-arm reaches backend");
    checkStatus(sensors.activate(1, true), 0, 0, "concurrent test activation");
    std::promise<void> entered, release, secondStarted;
    auto waitRelease = release.get_future();
    sensors.hf_.hook = [&] { entered.set_value(); waitRelease.wait(); };
    auto first = std::async(std::launch::async, [&] { return sensors.activate(1, false); });
    entered.get_future().wait();
    auto second = std::async(std::launch::async, [&] {
        secondStarted.set_value();
        return sensors.batch(1, 71, 73);
    });
    secondStarted.get_future().wait();
    require(second.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
            "concurrent batch waits for backend transaction");
    release.set_value();
    checkStatus(first.get(), 0, 0, "concurrent deactivate succeeds");
    checkStatus(second.get(), 0, 0, "concurrent batch succeeds");
    require(!sensors.snapshot(1).active && sensors.snapshot(1).samplingPeriodNs == 71,
            "concurrent batch uses current committed state");
}

#if defined(MINDONE_HF_RUST_WIRE)
void ffi() {
    mindone_hf_state state{10, 20, 1, 0};
    mindone_hf_state_transition output;
    std::memset(&output, 0xa5, sizeof(output));
    const auto unchanged = output;
    for (uint32_t flag : {2u, UINT32_MAX}) {
        require(mindone_hf_state_activate(&state, 1, flag, &output) == -1, "FFI enabled bool validated");
        require(mindone_hf_state_flush(&state, 1, flag, &output) == -1, "FFI one-shot bool validated");
        state.active = flag;
        require(mindone_hf_state_batch(&state, 1, 1, 2, &output) == -1, "FFI active bool validated");
        state.active = 1;
    }
    state.reserved = 1;
    require(mindone_hf_state_batch(&state, 1, 1, 2, &output) == -1, "FFI reserved state validated");
    state.reserved = 0;
    require(mindone_hf_state_activate(nullptr, 1, 1, &output) == -1, "FFI null state rejected");
    require(mindone_hf_state_batch(&state, 1, 1, 2, nullptr) == -1, "FFI null output rejected");
    alignas(8) uint8_t raw[64]{};
    require(mindone_hf_state_flush(reinterpret_cast<mindone_hf_state*>(raw + 1), 1, 0, &output) == -1,
            "FFI unaligned state rejected");
    require(mindone_hf_state_flush(&state, 1, 0, reinterpret_cast<mindone_hf_state_transition*>(raw + 1)) == -1,
            "FFI unaligned output rejected");
    require(std::memcmp(&output, &unchanged, sizeof(output)) == 0, "FFI errors retain output bytes");
    require(mindone_hf_state_flush(&state, 1, 0, &output) == 0 && output.command == MINDONE_HF_STATE_FLUSH,
            "FFI successful flush action");
    require(output.next.period_ns == 10 && output.next.latency_ns == 20 && output.next.active == 1 &&
                    output.next.reserved == 0 && output.reserved == 0, "FFI state layout and reserved fields");
    Sensors sensors;
    for (int64_t invalid : {INT64_MIN, int64_t(-1)}) {
        checkStatus(sensors.batch(1, invalid, 0), EX_ILLEGAL_ARGUMENT, 0, "negative period rejected");
        checkStatus(sensors.batch(1, 0, invalid), EX_ILLEGAL_ARGUMENT, 0, "negative latency rejected");
    }
    require(sensors.state_.empty() && sensors.hf_.calls.empty(), "invalid timing has no state or backend effect");
    sensors.hf_.fail = true;
    checkStatus(sensors.activate(1, true), EX_SERVICE_SPECIFIC, EIO, "first activation failure");
    require(sensors.state_.empty(), "first activation failure preserves absent state");
}
#endif

int main(int argc, char**) {
    if (argc > 1) {
        replay(true);
        return 0;
    }
    replay(false);
    lifecycle();
#if defined(MINDONE_HF_RUST_WIRE)
    ffi();
#endif
    std::cout << assertions << " assertions; 10000 exact state replays and lifecycle checks PASS\n";
}
