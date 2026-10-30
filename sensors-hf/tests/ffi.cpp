/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "HfRustCore.h"
#include "hf_manager_uapi.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <poll.h>
#include <string>
#include <vector>

static_assert(sizeof(void*) == 8);
static_assert(sizeof(mindone::hf::hf_manager_event) == 76);
static_assert(alignof(mindone::hf::hf_manager_event) == 4);
static_assert(offsetof(mindone::hf::hf_manager_event, word) == 12);
static_assert(sizeof(mindone::hf::hf_manager_cmd) == 52);
static_assert(alignof(mindone::hf::hf_manager_cmd) == 4);
static_assert(offsetof(mindone::hf::hf_manager_cmd, data) == 4);
static_assert(sizeof(mindone::hf::sensor_info) == 40);
static_assert(mindone::hf::SENSOR_TYPE_SENSOR_MAX == 78);
static_assert(sizeof(mindone_hf_decoded_event) == 88);
static_assert(alignof(mindone_hf_decoded_event) == 8);
static_assert(offsetof(mindone_hf_decoded_event, words) == 8);
static_assert(offsetof(mindone_hf_decoded_event, sensor_type) == 72);
static_assert(sizeof(mindone_hf_decoded_info) == 40);
static_assert(alignof(mindone_hf_decoded_info) == 4);
static_assert(offsetof(mindone_hf_decoded_info, gain) == 4);
static_assert(offsetof(mindone_hf_decoded_info, name) == 8);
static_assert(offsetof(mindone_hf_decoded_info, vendor) == 24);

size_t checks = 0;

void expect(bool condition, const char* name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
    ++checks;
}

void ignored_log(const char*, ...) {}

#define ALOGE(...) ignored_log(__VA_ARGS__)

std::vector<uint8_t> written;

ssize_t fixture_write(int fd, const void* input, size_t count) {
    expect(fd == 7 && count == 52, "production command uses exact fd and wire length");
    const auto* bytes = static_cast<const uint8_t*>(input);
    written.assign(bytes, bytes + count);
    return 0;
}

int poll_result = 1;
int poll_error = 0;
short poll_revents = POLLIN;
size_t read_calls = 0;
bool continuous = false;
bool interrupted = false;

struct ReadStep {
    ssize_t count;
    int error;
};

std::deque<ReadStep> read_steps;

int fixture_poll(struct pollfd* descriptor, nfds_t count, int timeout) {
    expect(count == 1 && descriptor->fd == 7 && timeout == 100,
           "production reader polls the owned fd with the requested timeout");
    descriptor->revents = poll_revents;
    errno = poll_error;
    return poll_result;
}

ssize_t fixture_read(int fd, void* output, size_t capacity) {
    expect(fd == 7, "production reader reads only its owned fd");
    ++read_calls;
    if (read_calls > 200) {
        errno = EIO;
        return -1;
    }
    if (interrupted) {
        errno = EINTR;
        return -1;
    }
    ReadStep step{0, 0};
    if (continuous) {
        step.count = capacity;
    } else if (!read_steps.empty()) {
        step = read_steps.front();
        read_steps.pop_front();
    }
    if (step.count > 0) {
        std::memset(output, 0, std::min(capacity, static_cast<size_t>(step.count)));
    }
    errno = step.error;
    return step.count;
}

namespace mindone::hf {
class HfManagerClient {
  public:
    bool setEnable(uint8_t sensorType, bool enabled, int64_t delayNs, int64_t latencyNs);
    bool requestFlush(uint8_t sensorType);
    bool configCalibration(uint8_t sensorType, const void* data, uint8_t length);
    bool waitAndReadEvents(int timeoutMs, std::vector<hf_manager_event>* out);

  private:
    int fd_ = 7;
};

PRODUCTION_FUNCTIONS
}

uint64_t rng_state = 51;

uint32_t random_word() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return static_cast<uint32_t>(rng_state);
}

void commands_and_events() {
    mindone::hf::HfManagerClient client;
    for (size_t trial = 0; trial < 10000; ++trial) {
        uint8_t sensor = 1 + random_word() % 77;
        uint8_t enabled = random_word() & 1;
        int64_t delay = random_word();
        int64_t latency = random_word();
        std::array<uint8_t, 54> command;
        command.fill(0xa5);
        expect(client.setEnable(sensor, enabled, delay, latency), "production enable accepted");
        expect(mindone_hf_enable_command(sensor, enabled, delay, latency,
                                        command.data() + 1, 52) == 0, "Rust enable accepted");
        expect(std::equal(written.begin(), written.end(), command.begin() + 1),
               "Rust enable matches exact production command bytes");
        expect(command.front() == 0xa5 && command.back() == 0xa5, "command output canaries");
        expect(client.requestFlush(sensor), "production flush accepted");
        expect(mindone_hf_flush_command(sensor, command.data() + 1, 52) == 0,
               "Rust flush accepted");
        expect(std::equal(written.begin(), written.end(), command.begin() + 1),
               "Rust flush matches exact production command bytes");
        std::array<uint8_t, 48> calibration;
        for (auto& value : calibration) value = random_word();
        uint8_t length = random_word() % 49;
        expect(client.configCalibration(sensor, calibration.data(), length),
               "production calibration accepted");
        expect(mindone_hf_calibration_command(sensor, calibration.data(), length,
                                             command.data() + 1, 52) == 0,
               "Rust calibration accepted");
        expect(std::equal(written.begin(), written.end(), command.begin() + 1),
               "Rust calibration matches exact production command bytes");

        mindone::hf::hf_manager_event raw{};
        raw.timestamp = static_cast<int64_t>((uint64_t{random_word()} << 32) | random_word());
        raw.sensor_type = sensor;
        raw.accurancy = random_word() & 3;
        raw.action = random_word() % 7;
        raw.padding[0] = 0xa5;
        for (auto& value : raw.word) value = random_word();
        std::array<uint8_t, 77> unaligned;
        std::memcpy(unaligned.data() + 1, &raw, sizeof(raw));
        mindone_hf_decoded_event decoded{};
        expect(mindone_hf_decode_event(unaligned.data() + 1, 76, &decoded) == 0,
               "Rust accepts unaligned wire input");
        expect(decoded.timestamp_ns == raw.timestamp && decoded.sensor_type == raw.sensor_type &&
                       decoded.accuracy == raw.accurancy && decoded.action == raw.action,
               "Rust preserves event metadata and signed timestamps");
        for (size_t index = 0; index < 16; ++index) {
            expect(decoded.words[index] == raw.word[index], "Rust preserves every signed word");
        }
    }
}

void ffi_rejects_invalid_pointers_and_lengths() {
    std::array<uint8_t, 76> raw{};
    raw[8] = 1;
    alignas(8) std::array<uint8_t, 96> storage;
    storage.fill(0xa5);
    auto* output = reinterpret_cast<mindone_hf_decoded_event*>(storage.data());
    for (size_t length = 0; length < 76; ++length) {
        expect(mindone_hf_decode_event(raw.data(), length, output) == -1,
               "partial event rejected through FFI");
        expect(std::all_of(storage.begin(), storage.end(), [](uint8_t v) { return v == 0xa5; }),
               "failed event decode leaves output unchanged");
    }
    expect(mindone_hf_decode_event(nullptr, 76, output) == -1, "null event input rejected");
    expect(mindone_hf_decode_event(raw.data(), 76, nullptr) == -1, "null event output rejected");
    expect(mindone_hf_decode_event(raw.data(), 76,
                                  reinterpret_cast<mindone_hf_decoded_event*>(storage.data() + 1)) == -1,
           "misaligned decoded output rejected before access");
    expect(mindone_hf_enable_command(1, 2, 0, 0, storage.data(), 52) == -1,
           "nonboolean enable rejected");
    expect(mindone_hf_enable_command(1, 1, -1, 0, storage.data(), 52) == -5,
           "negative sampling period rejected");
    expect(mindone_hf_flush_command(0, storage.data(), 52) == -2,
           "invalid sensor rejected");
    expect(mindone_hf_flush_command(1, storage.data(), 51) == -1,
           "short command output rejected");
    expect(mindone_hf_flush_command(1, nullptr, 52) == -1, "null command output rejected");
    expect(mindone_hf_calibration_command(1, nullptr, 1, storage.data(), 52) == -1,
           "null nonempty calibration input rejected");
    expect(mindone_hf_calibration_command(1, raw.data(), 49, storage.data(), 52) == -1,
           "oversized calibration input rejected");
    expect(std::all_of(storage.begin(), storage.end(), [](uint8_t v) { return v == 0xa5; }),
           "all FFI failures preserve caller output");
    expect(mindone_hf_calibration_command(1, nullptr, 0, storage.data(), 52) == 0,
           "null empty calibration accepted");
}

void reset_reader() {
    poll_result = 1;
    poll_error = 0;
    poll_revents = POLLIN;
    read_calls = 0;
    continuous = false;
    interrupted = false;
    read_steps.clear();
}

void info_ffi() {
    std::array<uint8_t, 41> input;
    input.fill(0xa5);
    input[1] = 5;
    const uint32_t gain = 0xf1a2b3c4;
    for (size_t index = 0; index < 4; ++index) input[5 + index] = gain >> (index * 8);
    mindone_hf_decoded_info decoded{};
    expect(mindone_hf_decode_info(input.data() + 1, 40, &decoded) == 0,
           "info accepts unaligned wire input");
    expect(decoded.sensor_type == 5 && decoded.gain == gain,
           "info decodes exact little endian metadata");
    expect(std::all_of(decoded.name, decoded.name + 16, [](uint8_t value) { return value == 0xa5; }) &&
                   std::all_of(decoded.vendor, decoded.vendor + 16, [](uint8_t value) { return value == 0xa5; }),
           "info preserves all sixteen name and vendor bytes without requiring a terminator");
    alignas(4) std::array<uint8_t, 48> storage;
    storage.fill(0xa5);
    auto* output = reinterpret_cast<mindone_hf_decoded_info*>(storage.data());
    for (size_t length : {size_t{0}, size_t{39}, size_t{41}}) {
        expect(mindone_hf_decode_info(input.data() + 1, length, output) == -1,
               "info rejects incorrect wire length");
    }
    expect(mindone_hf_decode_info(nullptr, 40, output) == -1, "info rejects null input");
    expect(mindone_hf_decode_info(input.data() + 1, 40, nullptr) == -1, "info rejects null output");
    expect(mindone_hf_decode_info(input.data() + 1, 40,
                                 reinterpret_cast<mindone_hf_decoded_info*>(storage.data() + 1)) == -1,
           "info rejects misaligned output");
    for (uint8_t sensor : {uint8_t{0}, uint8_t{78}, uint8_t{255}}) {
        input[1] = sensor;
        expect(mindone_hf_decode_info(input.data() + 1, 40, output) == -2,
               "info rejects invalid sensor types");
    }
    expect(std::all_of(storage.begin(), storage.end(), [](uint8_t value) { return value == 0xa5; }),
           "all failed info decodes leave the caller output unchanged");
}

void bounded_production_reader() {
    mindone::hf::HfManagerClient client;
    std::vector<mindone::hf::hf_manager_event> output(3);
    reset_reader();
    poll_result = 0;
    expect(client.waitAndReadEvents(100, &output) && output.empty() && read_calls == 0,
           "poll timeout clears output without reading");
    reset_reader();
    poll_result = -1;
    poll_error = EINTR;
    expect(client.waitAndReadEvents(100, &output) && read_calls == 0,
           "interrupted poll is benign");
    reset_reader();
    poll_result = -1;
    poll_error = EIO;
    expect(!client.waitAndReadEvents(100, &output) && read_calls == 0,
           "poll errors remain failures");
    reset_reader();
    poll_revents = POLLIN | POLLHUP;
    expect(!client.waitAndReadEvents(100, &output) && read_calls == 0,
           "poll hangup never reads a stale stream");
    reset_reader();
    read_steps.push_back({76, 0});
    expect(client.waitAndReadEvents(100, &output) && output.size() == 1 && read_calls == 1,
           "short complete-record batch is delivered");
    reset_reader();
    continuous = true;
    expect(client.waitAndReadEvents(100, &output) && output.size() == 128 && read_calls == 2,
           "continuous producer cannot monopolize the reader or grow output indefinitely");
    reset_reader();
    interrupted = true;
    expect(client.waitAndReadEvents(100, &output) && output.empty() && read_calls <= 16,
           "persistent EINTR remains bounded");
    for (ssize_t count : {ssize_t{1}, ssize_t{75}, ssize_t{77}, ssize_t{64 * 76 + 76}}) {
        reset_reader();
        read_steps.push_back({count, 0});
        expect(!client.waitAndReadEvents(100, &output) && output.empty() && read_calls == 1,
               "invalid read framing is a failure without partial delivery");
    }
    reset_reader();
    read_steps.push_back({64 * 76, 0});
    read_steps.push_back({-1, EIO});
    expect(!client.waitAndReadEvents(100, &output) && output.empty() && read_calls == 2,
           "later read failure clears the previously drained prefix");
}

int main() {
    commands_and_events();
    ffi_rejects_invalid_pointers_and_lengths();
    info_ffi();
    bounded_production_reader();
    std::printf("PASS %zu assertions; 10000 command/event replays; bounded production read controls\n", checks);
}
