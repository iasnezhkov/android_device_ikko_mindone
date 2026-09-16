/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Thin, direct wrapper around the single shared /dev/hf_manager fd this HAL
 * process opens once. One hf_client (kernel side, hf_manager.c:1246
 * hf_client_create()) is created per open() call/process, so this class is
 * meant to be instantiated exactly once by Sensors (one process = one
 * kernel client = one event FIFO shared by every activated sensor).
 *
 * Every ioctl/read/write here is a direct, undecorated translation of the
 * hf_manager ABI in hf_manager_uapi.h - see SENSORS-HAL-PLAN-1309.md for the
 * full protocol writeup with kernel file:line citations.
 */

#ifndef MINDONE_HF_MANAGER_CLIENT_H_
#define MINDONE_HF_MANAGER_CLIENT_H_

#include <cstdint>
#include <vector>

#include "hf_manager_uapi.h"

namespace mindone {
namespace hf {

class HfManagerClient {
  public:
    HfManagerClient() = default;
    ~HfManagerClient();

    HfManagerClient(const HfManagerClient&) = delete;
    HfManagerClient& operator=(const HfManagerClient&) = delete;

    // Opens /dev/hf_manager, retrying briefly if the node does not exist
    // yet. Returns false (logged) if it never appears.
    bool open();
    bool isOpen() const { return fd_ >= 0; }
    int fd() const { return fd_; }

    // HF_MANAGER_REQUEST_READY_STATUS (hf_manager.c:1628). ANDs
    // hf_device->ready across every registered hf_device; vacuously true
    // if no device has registered at all (see waitUntilReady()).
    bool queryReady();

    // HF_MANAGER_REQUEST_REGISTER_STATUS (hf_manager.c:1588): true iff
    // sensor_list_bitmap has this sensor_type's bit set, i.e. some
    // hf_device claimed it via hf_manager_create() (hf_manager.c:376-390).
    bool queryRegistered(uint8_t sensorType);

    // HF_MANAGER_REQUEST_SENSOR_INFO (hf_manager.c:1605): fails with
    // false/-EINVAL if the type is not registered. Only sensor_type, gain,
    // name, vendor are populated - see hf_manager_uapi.h::sensor_info.
    bool querySensorInfo(uint8_t sensorType, sensor_info* out);

    // Blocking (bounded) readiness gate. Combines queryReady() with an
    // explicit non-zero, count-stable enumeration to close the vacuous-
    // true gap documented in the .cpp. Returns true once both hold for two
    // consecutive polls in a row; returns false (logged) if timeoutMs
    // elapses first - callers should proceed with whatever
    // countRegisteredSensors() last saw rather than blocking forever.
    bool waitUntilReady(int timeoutMs, int pollIntervalMs = 300);

    // Counts sensor_types in [1, SENSOR_TYPE_SENSOR_MAX) that
    // queryRegistered() reports true for. One ioctl per candidate type
    // (72 of them) - cheap, fine to call every poll tick.
    int countRegisteredSensors();

    // write(fd, &cmd, sizeof(cmd)) with action=ENABLE/DISABLE
    // (hf_manager.c:1191-1202, hf_manager_write():1478). delayNs/latencyNs
    // are passed straight through to struct hf_manager_batch - the kernel
    // ABI is nanoseconds throughout (do NOT convert to microseconds here;
    // that conversion only happens for SensorInfo.minDelayUs/maxDelayUs).
    // Ignored by the kernel when enabled==false, but always populated for
    // symmetry.
    bool setEnable(uint8_t sensorType, bool enabled, int64_t delayNs, int64_t latencyNs);

    // write() with action=FLUSH (hf_manager.c:1203, hf_manager_write()).
    // Completion arrives asynchronously as an event with action==
    // FLUSH_ACTION in the read() stream, not as a return value here.
    bool requestFlush(uint8_t sensorType);

    // write() with action=CONFIG_CALI (hf_manager.c:1212-1218
    // HF_MANAGER_SENSOR_CONFIG_CALI, hf_manager_device_config_cali() ->
    // hf_device->config_cali() -> our sensorhub's transceiver_config(),
    // transceiver.c:685-706). `length` must be <= sizeof(hf_manager_cmd::
    // data) (48) and must exactly match the fixed per-sensor_type byte
    // layout the kernel expects (see CalibrationStore.h for the layout
    // table and its kernel-source citations) - a length that differs from
    // a PREVIOUSLY pushed length for the same sensor_type is rejected by
    // the kernel with -EINVAL (transceiver.c:697-701, cached cfg->length
    // mismatch), which surfaces here as a write() failure.
    bool configCalibration(uint8_t sensorType, const void* data, uint8_t length);

    // HF_MANAGER_REQUEST_BIAS_DATA / _CALI_DATA / _TEMP_DATA (hf_manager.c:
    // 1593-1601, ioctl_packet.status = enable): per-CLIENT (i.e. per-fd,
    // per-process) subscribe/unsubscribe toggle for BIAS_ACTION/CALI_ACTION/
    // TEMP_ACTION events on this sensor_type. REQUIRED before
    // waitAndReadEvents() will ever return one of these actions for this
    // fd: hf_manager_distinguish_event() (hf_manager.c:557-600) checks
    // client->request[sensor_type].{bias,cali,temp} and silently drops the
    // event for this client otherwise, independent of whether the sensor
    // itself is enabled. _IOW-only (kernel never copies anything back), so
    // these return true/false purely on the ioctl() call succeeding.
    bool requestBiasReports(uint8_t sensorType, bool enable);
    bool requestCaliReports(uint8_t sensorType, bool enable);
    bool requestTempReports(uint8_t sensorType, bool enable);

    // poll()s for up to timeoutMs for POLLIN (hf_manager_poll(),
    // hf_manager.c:1495), then drains every pending record with read()
    // into *out. IMPORTANT: hf_manager_read() (hf_manager.c:1450) never
    // blocks - it returns 0 immediately when the fifo is empty regardless
    // of O_NONBLOCK - so a bare read() loop without poll() first would
    // busy-spin. Returns false only on a real poll()/read() error (fd
    // closed, POLLERR/POLLHUP); a timeout with nothing to read returns
    // true with *out left empty.
    bool waitAndReadEvents(int timeoutMs, std::vector<hf_manager_event>* out);

  private:
    int fd_ = -1;
};

}  // namespace hf
}  // namespace mindone

#endif  // MINDONE_HF_MANAGER_CLIENT_H_
