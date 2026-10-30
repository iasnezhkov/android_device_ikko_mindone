/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <utility>

namespace mindone {

class LockoutStore {
  public:
    struct State {
        uint32_t failed = 0;
        uint32_t failedTimed = 0;
        int64_t deadlineMs = 0;
    };

    LockoutStore(int32_t userId, std::string directory)
        : userId_(userId), directory_(std::move(directory)), name_(std::to_string(userId)) {}

    bool load(State& state) {
        int dir = openDirectory();
        if (dir < 0) return false;
        Record marker{};
        const auto expected = encode({}, kMarkerMagic);
        int markerResult = readRecord(dir, name_ + ".seen", marker);
        Record record{};
        int recordResult = readRecord(dir, name_ + ".lockout", record);
        bool ok = false;
        if (markerResult == ENOENT && recordResult == ENOENT) {
            ok = writeRecord(dir, name_ + ".seen", expected) &&
                 writeRecord(dir, name_ + ".lockout", encode({}));
            state = {};
        } else if (recordResult == 0 && decode(record, state)) {
            ok = (markerResult == 0 && marker == expected) || state.failed == 20;
        }
        close(dir);
        return ok;
    }

    bool save(const State& state) {
        int dir = openDirectory();
        if (dir < 0) return false;
        bool ok = writeRecord(dir, name_ + ".seen", encode({}, kPendingMagic)) &&
                  writeRecord(dir, name_ + ".lockout", encode(state)) &&
                  writeRecord(dir, name_ + ".seen", encode({}, kMarkerMagic));
        close(dir);
        return ok;
    }

  private:
    using Record = std::array<uint8_t, 32>;
    static constexpr uint32_t kMagic = 0x314c5046;
    static constexpr uint32_t kMarkerMagic = 0x314d5046;
    static constexpr uint32_t kPendingMagic = 0x31575046;

    static uint32_t crc(const Record& record) {
        uint32_t value = 0xffffffff;
        for (size_t i = 0; i < 28; ++i) {
            value ^= record[i];
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
        }
        return ~value;
    }

    static void put(Record& record, size_t offset, uint64_t value, size_t size = 4) {
        for (size_t i = 0; i < size; ++i) record[offset + i] = value >> (8 * i);
    }

    static uint64_t get(const Record& record, size_t offset, size_t size = 4) {
        uint64_t value = 0;
        for (size_t i = 0; i < size; ++i) value |= uint64_t(record[offset + i]) << (8 * i);
        return value;
    }

    Record encode(const State& state, uint32_t magic = kMagic) const {
        Record record{};
        put(record, 0, magic);
        put(record, 4, 1);
        put(record, 8, userId_);
        put(record, 12, state.failed);
        put(record, 16, state.failedTimed);
        put(record, 20, state.deadlineMs, 8);
        put(record, 28, crc(record));
        return record;
    }

    bool decode(const Record& record, State& state) const {
        if (get(record, 0) != kMagic || get(record, 4) != 1 ||
            get(record, 8) != uint32_t(userId_) || get(record, 28) != crc(record)) return false;
        state.failed = get(record, 12);
        state.failedTimed = get(record, 16);
        uint64_t deadline = get(record, 20, 8);
        if (state.failed > 20 || state.failedTimed > 5 || state.failedTimed > state.failed ||
            deadline > INT64_MAX || (deadline != 0 && (state.failedTimed != 5 || state.failed >= 20)) ||
            (deadline == 0 && state.failedTimed >= 5)) return false;
        state.deadlineMs = deadline;
        return true;
    }

    int openDirectory() const {
        return open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }

    static int readRecord(int dir, const std::string& name, Record& record) {
        int fd = openat(dir, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) return errno;
        size_t used = 0;
        while (used < record.size()) {
            ssize_t count = read(fd, record.data() + used, record.size() - used);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            used += count;
        }
        uint8_t extra;
        ssize_t trailing;
        do { trailing = read(fd, &extra, 1); } while (trailing < 0 && errno == EINTR);
        close(fd);
        return used == record.size() && trailing == 0 ? 0 : EIO;
    }

    static bool writeRecord(int dir, const std::string& name, const Record& record) {
        const std::string temporary = name + ".tmp";
        int fd = openat(dir, temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) return false;
        size_t used = 0;
        while (used < record.size()) {
            ssize_t count = write(fd, record.data() + used, record.size() - used);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            used += count;
        }
        bool ok = used == record.size() && fsync(fd) == 0;
        if (close(fd) != 0) ok = false;
        if (ok) ok = renameat(dir, temporary.c_str(), dir, name.c_str()) == 0 && fsync(dir) == 0;
        if (!ok) unlinkat(dir, temporary.c_str(), 0);
        return ok;
    }

    int32_t userId_;
    std::string directory_;
    std::string name_;
};

}
