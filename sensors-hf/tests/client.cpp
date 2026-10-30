/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "HfManagerClient.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <poll.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

using namespace mindone::hf;

static_assert(sizeof(void*) == 8);
static_assert(sizeof(hf_manager_event) == 76);
static_assert(sizeof(hf_manager_cmd) == 52);
static_assert(sizeof(sensor_info) == 40);
static_assert(!std::is_copy_constructible_v<HfManagerClient>);
static_assert(!std::is_move_constructible_v<HfManagerClient>);

size_t checks = 0;

void expect(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}

struct Read {
    std::vector<uint8_t> bytes;
    ssize_t reported;
    int error;
};

struct Context {
    bool owned = false;
    int openError = 0;
    size_t missingAttempts = 0;
    size_t opens = 0;
    size_t closes = 0;
    size_t sleeps = 0;
    size_t ioctls = 0;
    bool ioctlError = false;
    int infoType = -1;
    bool zeroGain = false;
    bool writeError = false;
    int pollResult = 1;
    int pollError = 0;
    short pollEvents = POLLIN;
    size_t polls = 0;
    size_t reads = 0;
    bool continuous = false;
    bool interrupted = false;
    std::deque<Read> pending;
    std::vector<std::vector<uint8_t>> writes;
    std::vector<uint8_t> trace;
};

Context context;
constexpr int kOwnedFd = 713;

hf_manager_event event(uint8_t sensor, uint8_t action, int64_t stamp) {
    hf_manager_event value{};
    value.timestamp = stamp;
    value.sensor_type = sensor;
    value.accurancy = action == DATA_ACTION ? 3 : 255;
    value.action = action;
    value.padding[0] = 0xa5;
    for (size_t index = 0; index < 16; ++index) {
        value.word[index] = index % 2 ? INT32_MAX - index : INT32_MIN + index;
    }
    return value;
}

Read records(size_t count, uint8_t action = DATA_ACTION) {
    std::vector<uint8_t> bytes(count * sizeof(hf_manager_event));
    for (size_t index = 0; index < count; ++index) {
        const auto value = event(index % 2 ? 5 : 1, action, INT64_MIN + index);
        std::memcpy(bytes.data() + index * sizeof(value), &value, sizeof(value));
    }
    return {bytes, static_cast<ssize_t>(bytes.size()), 0};
}

void append(const void* input, size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(input);
    context.trace.insert(context.trace.end(), bytes, bytes + length);
}

void metadata(const sensor_info& info) {
    append(&info.sensor_type, 1);
    append(&info.gain, 4);
    append(info.name, 16);
    append(info.vendor, 16);
}

void delivered(const hf_manager_event& value) {
    hf_manager_event canonical = value;
    canonical.padding[0] = 0;
    append(&canonical, sizeof(canonical));
}

int fixture_open(const char* path, int flags) {
    expect(std::strcmp(path, "/dev/hf_manager") == 0 && flags == (O_RDWR | O_CLOEXEC),
           "actual open uses exact driver and close-on-exec flags");
    expect(!context.owned, "a client cannot steal another owned fd");
    ++context.opens;
    if (context.missingAttempts || context.openError) {
        errno = context.missingAttempts ? ENOENT : context.openError;
        if (context.missingAttempts) --context.missingAttempts;
        return -1;
    }
    context.owned = true;
    return kOwnedFd;
}

int fixture_close(int fd) {
    expect(context.owned && fd == kOwnedFd, "actual destructor closes only the owned fd");
    context.owned = false;
    ++context.closes;
    return 0;
}

int fixture_usleep(useconds_t duration) {
    expect(duration == 100000 || duration == 1000, "sleep is an explicitly bounded retry");
    ++context.sleeps;
    return 0;
}

int fixture_ioctl(int fd, unsigned long request, void* argument) {
    expect(context.owned && fd == kOwnedFd && argument, "ioctl uses the owned fd and packet");
    ++context.ioctls;
    if (context.ioctlError) {
        errno = EIO;
        return -1;
    }
    auto* packet = static_cast<ioctl_packet*>(argument);
    if (request == HF_MANAGER_REQUEST_READY_STATUS) {
        packet->status = true;
    } else if (request == HF_MANAGER_REQUEST_REGISTER_STATUS) {
        packet->status = packet->sensor_type == 1 || packet->sensor_type == 5;
    } else if (request == HF_MANAGER_REQUEST_SENSOR_INFO) {
        if (packet->sensor_type != 1 && packet->sensor_type != 5) {
            errno = EINVAL;
            return -1;
        }
        sensor_info info{};
        info.sensor_type = context.infoType < 0 ? packet->sensor_type : context.infoType;
        info.gain = context.zeroGain ? 0 : info.sensor_type == 1 ? 1000 : UINT32_MAX;
        std::memset(info.padding, 0x81, sizeof(info.padding));
        for (size_t index = 0; index < 16; ++index) {
            info.name[index] = 'A' + index;
            info.vendor[index] = static_cast<char>(0xe0 + index);
        }
        std::memcpy(packet->byte, &info, sizeof(info));
    } else {
        expect(request == HF_MANAGER_REQUEST_BIAS_DATA || request == HF_MANAGER_REQUEST_CALI_DATA ||
                       request == HF_MANAGER_REQUEST_TEMP_DATA,
               "only actual supported subscription ioctls are issued");
    }
    return 0;
}

ssize_t fixture_write(int fd, const void* input, size_t bytes) {
    expect(context.owned && fd == kOwnedFd && bytes == 52, "command uses owned fd and exact wire size");
    const auto* data = static_cast<const uint8_t*>(input);
    context.writes.emplace_back(data, data + bytes);
    append(data, bytes);
    if (context.writeError) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int fixture_poll(pollfd* descriptor, nfds_t count, int timeout) {
    expect(context.owned && count == 1 && descriptor->fd == kOwnedFd &&
                   descriptor->events == POLLIN && timeout == 100,
           "actual poll uses owned fd, timeout and readable mask");
    ++context.polls;
    descriptor->revents = context.pollEvents;
    errno = context.pollError;
    return context.pollResult;
}

ssize_t fixture_read(int fd, void* output, size_t capacity) {
    expect(context.owned && fd == kOwnedFd && capacity > 0 && capacity <= 64 * 76 &&
                   capacity % 76 == 0,
           "actual read uses owned fd and bounded complete-record capacity");
    ++context.reads;
    if (context.reads > 200) {
        errno = EIO;
        return -1;
    }
    if (context.interrupted) {
        errno = EINTR;
        return -1;
    }
    Read step{{}, 0, 0};
    if (context.continuous) step = records(capacity / 76);
    else if (!context.pending.empty()) {
        step = context.pending.front();
        context.pending.pop_front();
    }
    if (!step.bytes.empty()) {
        std::memcpy(output, step.bytes.data(), std::min(capacity, step.bytes.size()));
    }
    errno = step.error;
    return step.reported;
}

PRODUCTION_CLIENT

void command(uint8_t sensor, uint8_t action, int64_t delay = 0, int64_t latency = 0,
             const uint8_t* calibration = nullptr, uint8_t length = 0) {
    hf_manager_cmd expected{};
    expected.sensor_type = sensor;
    expected.action = action;
    if (action == HF_MANAGER_SENSOR_ENABLE || action == HF_MANAGER_SENSOR_DISABLE) {
        hf_manager_batch timing{delay, latency};
        expected.length = sizeof(timing);
        std::memcpy(expected.data, &timing, sizeof(timing));
    } else if (action == HF_MANAGER_SENSOR_CONFIG_CALI) {
        expected.length = length;
        if (length) std::memcpy(expected.data, calibration, length);
    }
    expect(!context.writes.empty() &&
                   std::memcmp(context.writes.back().data(), &expected, sizeof(expected)) == 0,
           "actual command matches kernel UAPI including reserved and unused bytes");
}

void lifecycle() {
    context = {};
    { HfManagerClient client; expect(!client.isOpen() && client.fd() == -1, "constructor has no fd"); }
    expect(context.opens == 0 && context.closes == 0, "unopened destructor performs no close");
    context.openError = EACCES;
    { HfManagerClient client; expect(!client.open(), "permanent open error"); }
    expect(context.opens == 1 && context.closes == 0 && context.sleeps == 0,
           "permanent open error never retries or closes an unowned fd");
    context = {};
    context.missingAttempts = 100;
    { HfManagerClient client; expect(!client.open(), "missing device fails after retry budget"); }
    expect(context.opens == 20 && context.sleeps == 20 && context.closes == 0,
           "missing device open has a finite retry budget");
    context = {};
    context.missingAttempts = 2;
    { HfManagerClient client; expect(client.open(), "delayed device open"); }
    expect(context.opens == 3 && context.sleeps == 2 && context.closes == 1 && !context.owned,
           "owned fd closed exactly once");
}

void valid_cycle() {
    context = {};
    {
        HfManagerClient client;
        expect(client.open() && client.isOpen() && client.fd() == kOwnedFd, "open full-cycle client");
        expect(client.queryReady() && client.queryRegistered(1) && client.queryRegistered(5) &&
                       !client.queryRegistered(77) && client.countRegisteredSensors() == 2 &&
                       client.waitUntilReady(100, 1),
               "actual ready and registration methods retain their contract");
        for (uint8_t sensor : {uint8_t{1}, uint8_t{5}}) {
            sensor_info info{};
            expect(client.querySensorInfo(sensor, &info) && info.sensor_type == sensor &&
                           info.gain == (sensor == 1 ? 1000u : UINT32_MAX),
                   "actual metadata preserves full unsigned gain and sensor identity");
            for (size_t index = 0; index < 16; ++index) {
                expect(info.name[index] == static_cast<char>('A' + index) &&
                               static_cast<uint8_t>(info.vendor[index]) == 0xe0 + index,
                       "actual metadata preserves nonterminated name and vendor bytes");
            }
            metadata(info);
        }
        context.zeroGain = true;
        sensor_info zero{};
        expect(client.querySensorInfo(5, &zero) && zero.gain == 0,
               "actual metadata preserves zero gain without inventing a hardware value");
        metadata(zero);
        context.zeroGain = false;
        expect(client.requestBiasReports(1, true) && client.requestCaliReports(1, true) &&
                       client.requestTempReports(1, true), "calibration subscriptions remain C++ ioctls");
        expect(client.setEnable(1, true, 10000000, 50000000), "enable cycle");
        command(1, HF_MANAGER_SENSOR_ENABLE, 10000000, 50000000);
        std::array<uint8_t, 48> calibration;
        for (size_t index = 0; index < calibration.size(); ++index) calibration[index] = 0x80 + index;
        expect(client.configCalibration(1, calibration.data(), 48), "maximum calibration cycle");
        command(1, HF_MANAGER_SENSOR_CONFIG_CALI, 0, 0, calibration.data(), 48);
        std::vector<hf_manager_event> output;
        for (uint8_t action = DATA_ACTION; action < MAX_ACTION; ++action) {
            context.pending.push_back(records(3, action));
            expect(client.waitAndReadEvents(100, &output) && output.size() == 3,
                   "actual client delivers every declared event action");
            for (size_t index = 0; index < output.size(); ++index) {
                const auto expected = event(index % 2 ? 5 : 1, action, INT64_MIN + index);
                const auto& value = output[index];
                expect(value.timestamp == expected.timestamp && value.sensor_type == expected.sensor_type &&
                               value.action == expected.action && value.accurancy == expected.accurancy,
                       "actual event metadata and signed timestamp preserved");
                for (size_t word = 0; word < 16; ++word) {
                    expect(value.word[word] == expected.word[word], "actual event preserves all signed words");
                }
                delivered(value);
            }
        }
        for (int64_t index = 0; index < 256; ++index) {
            const uint8_t sensor = 1 + index % 77;
            expect(client.setEnable(sensor, index % 2, index * 1000000, index * 9000000),
                   "valid command replay");
            command(sensor, index % 2 ? HF_MANAGER_SENSOR_ENABLE : HF_MANAGER_SENSOR_DISABLE,
                    index * 1000000, index * 9000000);
            const uint8_t length = index % 49;
            expect(client.configCalibration(sensor, calibration.data(), length), "calibration length replay");
            command(sensor, HF_MANAGER_SENSOR_CONFIG_CALI, 0, 0, calibration.data(), length);
        }
        expect(client.requestFlush(1), "flush cycle");
        command(1, HF_MANAGER_SENSOR_FLUSH);
        context.pending.push_back(records(1, FLUSH_ACTION));
        expect(client.waitAndReadEvents(100, &output) && output.size() == 1 &&
                       output.front().action == FLUSH_ACTION, "flush completion retains old event API");
        delivered(output.front());
        expect(client.setEnable(1, false, 10000000, 50000000), "disable cycle");
        command(1, HF_MANAGER_SENSOR_DISABLE, 10000000, 50000000);
    }
    expect(context.closes == 1 && !context.owned, "owned fd closed exactly once");
}

void failures() {
    context = {};
    {
        HfManagerClient client;
        expect(client.open(), "open failure-control client");
        context.writeError = true;
        expect(!client.setEnable(1, true, 1, 2) && !client.requestFlush(1) &&
                       !client.configCalibration(1, nullptr, 0), "actual writes propagate negative errno");
        expect(context.writes.size() == 3, "failed writes are attempted exactly once");
        context.writeError = false;
        expect(client.setEnable(1, true, 1, 2), "zero write return remains success");
        sensor_info info;
        std::memset(&info, 0xa5, sizeof(info));
        const auto unchanged = info;
        context.ioctlError = true;
        expect(!client.querySensorInfo(1, &info) && std::memcmp(&info, &unchanged, sizeof(info)) == 0,
               "failed metadata ioctl preserves output");
        expect(!client.requestBiasReports(1, true) && !client.requestCaliReports(1, true) &&
                       !client.requestTempReports(1, true), "subscription errors remain failures");
        context.ioctlError = false;
        std::vector<hf_manager_event> output(3);
        context.pollResult = 0;
        expect(client.waitAndReadEvents(100, &output) && output.empty() && context.reads == 0,
               "poll timeout clears output without reads");
        context.pollResult = -1;
        context.pollError = EINTR;
        expect(client.waitAndReadEvents(100, &output) && output.empty(), "poll EINTR remains benign");
        context.pollError = EIO;
        expect(!client.waitAndReadEvents(100, &output) && output.empty(), "poll error propagation");
        context.pollResult = 1;
        for (short flags : {short(POLLIN | POLLHUP), short(POLLERR), short(POLLNVAL)}) {
            context.pollEvents = flags;
            expect(!client.waitAndReadEvents(100, &output) && output.empty() && context.reads == 0,
                   "poll errors never read the stream");
        }
        context.pollEvents = POLLIN;
        for (ssize_t bytes : {ssize_t{1}, ssize_t{75}, ssize_t{77}, ssize_t{65 * 76}}) {
            context.pending.push_back({{}, bytes, 0});
            expect(!client.waitAndReadEvents(100, &output) && output.empty(), "partial or oversized read rejected");
        }
        context.pending.push_back(records(64));
        context.pending.push_back({{}, -1, EIO});
        expect(!client.waitAndReadEvents(100, &output) && output.empty(), "read failure clears previous prefix");
        context.reads = 0;
        context.continuous = true;
        expect(client.waitAndReadEvents(100, &output) && output.size() == 128 && context.reads == 2,
               "continuous producer has bounded output and reads");
        context.continuous = false;
        context.reads = 0;
        context.interrupted = true;
        expect(client.waitAndReadEvents(100, &output) && output.empty() && context.reads == 16,
               "persistent read EINTR has a finite budget");
        context.interrupted = false;
#if defined(MINDONE_HF_RUST_WIRE)
        size_t writes = context.writes.size();
        expect(!client.setEnable(0, true, 1, 2) && !client.setEnable(78, true, 1, 2) &&
                       !client.setEnable(1, true, -1, 0) && !client.setEnable(1, true, 0, -1) &&
                       !client.requestFlush(0) && !client.configCalibration(1, nullptr, 1) &&
                       context.writes.size() == writes, "invalid Rust command is rejected before write");
        const size_t ioctls = context.ioctls;
        expect(!client.querySensorInfo(1, nullptr) && context.ioctls == ioctls,
               "null info output is rejected before ioctl");
        for (int sensor : {5, 0, 78}) {
            context.infoType = sensor;
            expect(!client.querySensorInfo(1, &info) && std::memcmp(&info, &unchanged, sizeof(info)) == 0,
                   "invalid or mismatched info rejected without output");
        }
        context.infoType = -1;
        for (int corruption = 0; corruption < 3; ++corruption) {
            context.pending.push_back(records(64));
            auto bad = records(1);
            if (corruption == 0) bad.bytes[8] = 78;
            if (corruption == 1) bad.bytes[10] = MAX_ACTION;
            if (corruption == 2) bad.bytes[9] = 4;
            context.pending.push_back(bad);
            expect(!client.waitAndReadEvents(100, &output) && output.empty(),
                   "failed Rust decode clears the entire earlier prefix");
        }
#endif
    }
    expect(context.closes == 1 && !context.owned, "failure-control fd closed exactly once");
}

int main(int argc, char**) {
    lifecycle();
    valid_cycle();
    if (argc == 2) {
        for (uint8_t byte : context.trace) std::printf("%02x", byte);
        std::printf("\n");
    } else {
        failures();
        std::printf("PASS %zu assertions; real client lifecycle, wire cycle and bounded error controls\n", checks);
    }
}
