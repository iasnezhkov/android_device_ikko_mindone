/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone.sensors.hf"

#include "HfManagerClient.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>

#include <log/log.h>

namespace mindone {
namespace hf {

namespace {

constexpr const char* kDevPath = "/dev/hf_manager";
constexpr int kOpenRetries = 20;
constexpr int kOpenRetryDelayMs = 100;  // 20 x 100ms = 2s max wait for the node

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

}  // namespace

HfManagerClient::~HfManagerClient() {
    if (fd_ >= 0) ::close(fd_);
}

bool HfManagerClient::open() {
    for (int attempt = 0; attempt < kOpenRetries; ++attempt) {
        fd_ = ::open(kDevPath, O_RDWR | O_CLOEXEC);
        if (fd_ >= 0) {
            if (attempt > 0) {
                ALOGI("%s appeared after %d ms", kDevPath, attempt * kOpenRetryDelayMs);
            }
            return true;
        }
        if (errno != ENOENT) {
            ALOGE("open(%s) failed: %s", kDevPath, strerror(errno));
            return false;
        }
        usleep(kOpenRetryDelayMs * 1000);
    }
    ALOGE("%s never appeared after %d ms - hf_manager.ko not loaded?", kDevPath,
          kOpenRetries * kOpenRetryDelayMs);
    return false;
}

bool HfManagerClient::queryReady() {
    ioctl_packet packet{};
    packet.sensor_type = SENSOR_TYPE_INVALID;  // unused by this ioctl, must be < SENSOR_TYPE_SENSOR_MAX
    if (::ioctl(fd_, HF_MANAGER_REQUEST_READY_STATUS, &packet) < 0) {
        ALOGE("HF_MANAGER_REQUEST_READY_STATUS failed: %s", strerror(errno));
        return false;
    }
    return packet.status;
}

bool HfManagerClient::queryRegistered(uint8_t sensorType) {
    ioctl_packet packet{};
    packet.sensor_type = sensorType;
    if (::ioctl(fd_, HF_MANAGER_REQUEST_REGISTER_STATUS, &packet) < 0) {
        return false;
    }
    return packet.status;
}

bool HfManagerClient::querySensorInfo(uint8_t sensorType, sensor_info* out) {
    ioctl_packet packet{};
    packet.sensor_type = sensorType;
    if (::ioctl(fd_, HF_MANAGER_REQUEST_SENSOR_INFO, &packet) < 0) {
        // Expected/frequent: -EINVAL for any sensor_type not yet (or never)
        // registered. Not logged as an error to avoid spamming during the
        // readiness poll and during enumeration of unsupported type IDs.
        return false;
    }
    static_assert(sizeof(sensor_info) <= sizeof(packet.byte),
                  "sensor_info must fit in ioctl_packet.byte[64]");
    memcpy(out, packet.byte, sizeof(sensor_info));
    return true;
}

int HfManagerClient::countRegisteredSensors() {
    int count = 0;
    for (int t = 1; t < SENSOR_TYPE_SENSOR_MAX; ++t) {
        if (queryRegistered(static_cast<uint8_t>(t))) ++count;
    }
    return count;
}

bool HfManagerClient::waitUntilReady(int timeoutMs, int pollIntervalMs) {
    // Boot-race context (full citation chain in
    // SENSORS-HAL-PLAN-1309.md#readiness-wait-design):
    //   sensorhub.ko module_init -> transceiver_init() calls
    //   hf_device_register() unconditionally (transceiver.c:1040), which
    //   adds a ready=false entry to hfcore.device_list (hf_manager.c:309-
    //   320) at ordinary module-load time - long before SCP is up.
    //   Only once SCP signals SCP_EVENT_READY AND the SCP sensor task acks
    //   back over sensor_comm (ready.c:47-98, the two flags scp_platform_
    //   ready/scp_sensor_ready both true) does the sensor_ready notifier
    //   chain fire transceiver_ready_notifier (transceiver.c:935-947,
    //   priority READY_HIGHPRI) -> transceiver_sensor_bootup() ->
    //   transceiver_create_manager() (transceiver.c:891-907), which first
    //   does a synchronous SCP round-trip to fetch the REAL sensor list
    //   (sensor_list_get_list(), sensorhub/sensor_list.c:122) and only
    //   THEN calls hf_manager_create(), which is what flips
    //   hf_dev->ready=true (hf_manager.c:454). So by construction: the
    //   instant HF_MANAGER_REQUEST_READY_STATUS turns true, the real list
    //   is already fully populated - no extra settle time is needed after
    //   ready flips.
    //   The empirically measured F4290/F4292 ~27s gap is SCP core boot +
    //   firmware handshake, all upstream of any of this; nothing here can
    //   shorten it, only wait it out without giving Android a permanently
    //   empty sensor list.
    //
    // Defensive addition beyond the literal ioctl: READY_STATUS returns
    // vacuously true if hfcore.device_list is empty (hf_manager.c:1630,
    // packet.status pre-set true, loop body never runs to falsify it) -
    // i.e. before sensorhub.ko's module_init has even run. We therefore
    // also require a non-zero, two-poll-stable registered-sensor count
    // before declaring readiness, so a future change to module load order
    // can't reintroduce the exact bug this class exists to fix.
    const int64_t deadline = nowMs() + timeoutMs;
    int lastCount = -1;
    int stableHits = 0;
    while (nowMs() < deadline) {
        bool ready = queryReady();
        int count = countRegisteredSensors();
        ALOGI("hf_manager readiness poll: ready=%d registered_count=%d", ready, count);
        if (ready && count > 0) {
            if (count == lastCount) {
                if (++stableHits >= 2) {
                    ALOGI("hf_manager ready: %d sensors registered", count);
                    return true;
                }
            } else {
                lastCount = count;
                stableHits = 1;
            }
        } else {
            lastCount = -1;
            stableHits = 0;
        }
        usleep(pollIntervalMs * 1000);
    }
    ALOGE("hf_manager NOT ready after %dms (last count=%d) - proceeding anyway, "
          "getSensorsList() may be incomplete",
          timeoutMs, lastCount);
    return false;
}

bool HfManagerClient::setEnable(uint8_t sensorType, bool enabled, int64_t delayNs,
                                 int64_t latencyNs) {
    hf_manager_cmd cmd{};
    cmd.sensor_type = sensorType;
    cmd.action = enabled ? HF_MANAGER_SENSOR_ENABLE : HF_MANAGER_SENSOR_DISABLE;
    cmd.down_sample = 0;
    hf_manager_batch batch{delayNs, latencyNs};
    cmd.length = sizeof(batch);
    memcpy(cmd.data, &batch, sizeof(batch));
    // hf_manager_write() (hf_manager.c) returns hf_manager_drive_device()'s result, i.e. 0 on
    // success and a negative errno on failure - not the byte count. Only n < 0 is a failure, and
    // errno is meaningful only then.
    ssize_t n = ::write(fd_, &cmd, sizeof(cmd));
    if (n < 0) {
        ALOGE("hf_manager write(ENABLE type=%u en=%d) failed: %s", sensorType, enabled,
              strerror(errno));
        return false;
    }
    return true;
}

bool HfManagerClient::requestFlush(uint8_t sensorType) {
    hf_manager_cmd cmd{};
    cmd.sensor_type = sensorType;
    cmd.action = HF_MANAGER_SENSOR_FLUSH;
    cmd.down_sample = 0;
    cmd.length = 0;
    ssize_t n = ::write(fd_, &cmd, sizeof(cmd));  // 0 on success, see setEnable()
    if (n < 0) {
        ALOGE("hf_manager write(FLUSH type=%u) failed: %s", sensorType, strerror(errno));
        return false;
    }
    return true;
}

bool HfManagerClient::configCalibration(uint8_t sensorType, const void* data, uint8_t length) {
    hf_manager_cmd cmd{};
    if (length > sizeof(cmd.data)) {
        ALOGE("hf_manager configCalibration(type=%u): length=%u exceeds cmd.data (%zu)",
              sensorType, length, sizeof(cmd.data));
        return false;
    }
    cmd.sensor_type = sensorType;
    cmd.action = HF_MANAGER_SENSOR_CONFIG_CALI;
    cmd.down_sample = 0;
    cmd.length = length;
    if (length > 0) memcpy(cmd.data, data, length);
    ssize_t n = ::write(fd_, &cmd, sizeof(cmd));  // 0 on success, see setEnable()
    if (n < 0) {
        ALOGE("hf_manager write(CONFIG_CALI type=%u length=%u) failed: %s", sensorType, length,
              strerror(errno));
        return false;
    }
    return true;
}

namespace {
bool requestCaliData(int fd, unsigned int ioctlCmd, uint8_t sensorType, bool enable,
                      const char* what) {
    ioctl_packet packet{};
    packet.sensor_type = sensorType;
    packet.status = enable;
    if (::ioctl(fd, ioctlCmd, &packet) < 0) {
        ALOGE("hf_manager %s(type=%u, enable=%d) failed: %s", what, sensorType, enable,
              strerror(errno));
        return false;
    }
    return true;
}
}  // namespace

bool HfManagerClient::requestBiasReports(uint8_t sensorType, bool enable) {
    return requestCaliData(fd_, HF_MANAGER_REQUEST_BIAS_DATA, sensorType, enable,
                            "REQUEST_BIAS_DATA");
}

bool HfManagerClient::requestCaliReports(uint8_t sensorType, bool enable) {
    return requestCaliData(fd_, HF_MANAGER_REQUEST_CALI_DATA, sensorType, enable,
                            "REQUEST_CALI_DATA");
}

bool HfManagerClient::requestTempReports(uint8_t sensorType, bool enable) {
    return requestCaliData(fd_, HF_MANAGER_REQUEST_TEMP_DATA, sensorType, enable,
                            "REQUEST_TEMP_DATA");
}

bool HfManagerClient::waitAndReadEvents(int timeoutMs, std::vector<hf_manager_event>* out) {
    out->clear();
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int r = ::poll(&pfd, 1, timeoutMs);
    if (r < 0) {
        if (errno == EINTR) return true;  // benign, caller loops again
        ALOGE("poll(hf_manager) failed: %s", strerror(errno));
        return false;
    }
    if (r == 0) return true;  // timeout, nothing pending
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        ALOGE("poll(hf_manager) returned error revents=0x%x", pfd.revents);
        return false;
    }
    if (!(pfd.revents & POLLIN)) return true;

    // hf_manager_read() always returns a whole number of records (or 0);
    // drain everything currently queued before returning to the caller so
    // we don't fall behind the kernel FIFO (HF_CLIENT_FIFO_SIZE=128,
    // hf_manager.h:27) under load.
    constexpr size_t kBatch = 64;
    std::vector<hf_manager_event> buf(kBatch);
    for (;;) {
        ssize_t n = ::read(fd_, buf.data(), buf.size() * sizeof(hf_manager_event));
        if (n < 0) {
            if (errno == EINTR) continue;
            ALOGE("read(hf_manager) failed: %s", strerror(errno));
            return false;
        }
        if (n == 0) break;  // fifo drained
        size_t count = static_cast<size_t>(n) / sizeof(hf_manager_event);
        out->insert(out->end(), buf.begin(), buf.begin() + count);
        if (count < kBatch) break;  // short read == fifo drained this round
    }
    return true;
}

}  // namespace hf
}  // namespace mindone
