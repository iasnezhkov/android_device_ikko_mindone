/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINDONE_CALIBRATION_STORE_H_
#define MINDONE_CALIBRATION_STORE_H_

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "HfManagerClient.h"
#include "hf_manager_uapi.h"

namespace mindone {
namespace hf {

class CalibrationStore {
  public:
    // Byte offset/length of one sub-region within a sensor_type's CONFIG_
    // CALI payload. {0, 0} means "this sensor_type has no such region."
    struct RegionLayout {
        uint8_t offset;
        uint8_t length;  // always a multiple of 4 (int32 fields)
    };

    // One row of the fixed table derived from transceiver_update_config()
    // (see the file header comment). `name` is a short lowercase tag used
    // only for logging and for deriving the (currently stock-file-less)
    // temp-region filename "<name>_temp.json" if/when we ever persist one.
    struct SensorLayout {
        uint8_t sensorType;
        uint8_t totalLength;
        RegionLayout bias;
        RegionLayout cali;
        RegionLayout temp;
        const char* name;
        const char* biasFile;  // nullptr if no stock filename is known
        const char* biasKey;
        const char* caliFile;
        const char* caliKey;
    };

    explicit CalibrationStore(std::string baseDir = "/mnt/vendor/nvcfg/sensor");

    // Builds each calibratable sensor_type's CONFIG_CALI payload from the
    // nvcfg JSON files (missing file -> that region stays zero-filled,
    // matching the "presumably defaults" behavior the stock HAL shows for
    // accel/light on this device - see doc §Defaults) and pushes it via
    // client.configCalibration(). Also subscribes this client to
    // BIAS_ACTION/CALI_ACTION/TEMP_ACTION for each pushed sensor_type so
    // onKernelEvent() below actually receives anything - see the ioctl
    // subscribe requirement in the file header comment.
    //
    // Calls client.queryRegistered() itself for each of the four managed
    // sensor_types and skips any that come back false, so the caller does
    // not need to plumb its own enumeration results through here - a plain
    // `calibrationStore_.pushAll(hf_);` after the sensor is known ready is
    // enough (pushing to an unregistered type would otherwise be harmless -
    // hf_manager_drive_device()'s hf_manager_find_manager() lookup fails
    // closed with -EINVAL, logged by HfManagerClient as a write() error -
    // but it is needless log noise and an extra failed write() per boot).
    void pushAll(HfManagerClient& client);

    // Feed every hf_manager_event read off /dev/hf_manager here (from the
    // existing reader thread, alongside translateEvent()). Events for a
    // sensor_type this store does not manage, or actions other than BIAS_
    // ACTION/CALI_ACTION, are ignored. Thread-safe; safe to call from the
    // reader thread while pushAll() might be called concurrently from
    // ensureEnumeratedLocked() on a re-enumeration.
    //
    // NOTE: does not itself decide whether the event should also reach the
    // AIDL event queue - v1's rule (BIAS/CALI/TEMP/TEST/RAW are internal,
    // not surfaced) is unchanged in v2; see the Sensors.cpp hook diff.
    void onKernelEvent(const hf_manager_event& ke);

    // Exposed for tests and for the Sensors.cpp hook diff (to know which
    // sensor_types this store manages without hardcoding the list twice).
    static const SensorLayout* layoutFor(uint8_t sensorType);
    static const std::array<SensorLayout, 4>& allLayouts();

  private:
    struct CachedRegion {
        std::vector<int32_t> values;
        int64_t lastWriteMs = 0;
        bool haveValue = false;
    };

    // Minimum time between two nvcfg writes for the SAME region (bias or
    // cali) of the SAME sensor_type, when the incoming value keeps
    // changing. Guards flash wear against a hypothetical runaway SCP-side
    // auto-recalibration loop; a real factory/runtime calibration result is
    // expected at most a handful of times per boot (task premise: "this is
    // what the stock HAL does when the SCP finishes a runtime calibration",
    // i.e. an occasional, explicit event, not a stream). An IDENTICAL value
    // is always a no-op regardless of this timer (see persistRegion()).
    static constexpr int64_t kMinRewriteIntervalMs = 2000;

    std::vector<uint8_t> buildPayload(const SensorLayout& layout);
    // Records the values read from disk so a later kernel report carrying the same
    // values is recognised as unchanged (no rewrite). Requires mutex_ held.
    static void seedCache(CachedRegion* cache, const std::vector<int32_t>& values);
    // Never persists an all-zero region (see the .cpp) - factory files are non-zero.
    void persistRegion(const SensorLayout& layout, bool isBias, const int32_t* words,
                        size_t count);

    const std::string baseDir_;
    std::mutex mutex_;
    std::unordered_map<uint8_t, CachedRegion> biasCache_;
    std::unordered_map<uint8_t, CachedRegion> caliCache_;
};

}  // namespace hf
}  // namespace mindone

#endif  // MINDONE_CALIBRATION_STORE_H_
