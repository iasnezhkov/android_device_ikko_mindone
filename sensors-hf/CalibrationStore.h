/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * v2 calibration support for the mind_one AIDL sensors HAL.
 *
 * v1 (device/ikko/mindone/sensors-hf/) never pushes HF_MANAGER_SENSOR_
 * CONFIG_CALI at all, so accel/gyro/mag/light run on the SCP firmware's
 * built-in defaults instead of this device's factory-measured offsets. The
 * stock blob (sensors.mediatek.V2.0.so + android.hardware.sensors@2.X-
 * subhal-mediatek.so) reads the *.json files under /mnt/vendor/nvcfg/sensor/ at startup and
 * pushes CONFIG_CALI for exactly four sensor_types, observed live in dmesg
 * (our kernel's CONFIG_CALI branch prints this - hf_manager.c:1212-1218):
 *
 *     sar sensor_type:1 length:24    (SENSOR_TYPE_ACCELEROMETER)
 *     sar sensor_type:4 length:48    (SENSOR_TYPE_GYROSCOPE)
 *     sar sensor_type:2 length:36    (SENSOR_TYPE_MAGNETIC_FIELD)
 *     sar sensor_type:5 length:4     (SENSOR_TYPE_LIGHT)
 *
 * CalibrationStore reproduces that push (deliverable A) and additionally
 * closes the reverse loop the stock HAL also implements: persisting a
 * runtime calibration RESULT (SCP -> kernel BIAS_ACTION/CALI_ACTION event)
 * back into the same nvcfg JSON files, which v1 does not attempt at all.
 *
 * ---------------------------------------------------------------------
 * Payload byte layout - HOW WE KNOW THIS (high confidence, kernel-source
 * derived, not guessed): our own sensorhub module does not just forward the
 * CONFIG_CALI bytes opaquely - it also CACHES them per sensor_type
 * (transceiver_config(), transceiver.c:685-706, struct transceiver_config,
 * transceiver.c:30-36) so that a LATER BIAS_ACTION/CALI_ACTION/TEMP_ACTION
 * event coming back from the SCP can be spliced into the SAME buffer at
 * fixed, sensor_type-specific byte offsets (transceiver_update_config(),
 * transceiver.c:212-249, calling transceiver_copy_config() at
 * transceiver.c:186-208). Those offsets are declared right there in the
 * kernel switch statement:
 *
 *   SENSOR_TYPE_ACCELEROMETER: copy_config(dst, src, bias_len=12, cali_len=12, temp_len=0)   -> 24B total
 *   SENSOR_TYPE_MAGNETIC_FIELD: copy_config(dst, src, bias_len=12, cali_len=24, temp_len=0)   -> 36B total
 *   SENSOR_TYPE_GYROSCOPE:      copy_config(dst, src, bias_len=12, cali_len=12, temp_len=24)  -> 48B total
 *   default (LIGHT and everything else not special-cased above): the WHOLE
 *     buffer is treated as the "cali" region for a CALI_ACTION splice
 *     (transceiver.c:243-246) - for LIGHT that whole buffer is 4 bytes
 *     (dst->length <= sizeof(src->word) is the only guard), i.e. one int32,
 *     no bias/temp concept.
 *
 * These byte counts match the dmesg lengths above EXACTLY (24/48/36/4), and
 * because transceiver_config()'s cache and the very bytes a userspace
 * CONFIG_CALI write supplies are the *same buffer* (transceiver.c:695-703:
 * `cfg->length = length; memcpy(cfg->data, data, length);` immediately
 * followed by forwarding that same `data`/`length` to the SCP), the offset
 * convention the kernel uses to splice a LATER SCP result back in is, by
 * construction, the same layout the INITIAL push must already use - a
 * mismatch there would silently corrupt the cache and misplace runtime
 * updates relative to what the stock HAL (which relies on exactly this
 * splice) expects. All int32, little-endian (AArch64), matching the
 * gain-gated raw-value convention already confirmed for DATA_ACTION events
 * (F4294: value = raw_word / gain).
 *
 * The ACCELEROMETER row above is an EXPLICIT case in that kernel switch
 * statement (bias_len=12, cali_len=12) - it does not fall through to the
 * "whole buffer is cali" default branch that LIGHT and any unlisted type
 * gets. So a stock acc_cali.json is expected to hold exactly 3 values (the
 * cali region only), the same split-file shape as gyro_bias.json/
 * gyro_cali.json, not a 6-value combined bias+cali blob mirroring
 * mag_cali.json's own shape. No acc_cali.json sample exists on this device
 * to check directly (no factory accel cal was ever run here), so
 * buildPayload() below still auto-detects a 6-value file too, purely as a
 * harmless defensive fallback - not because the layout is genuinely in
 * doubt.
 *
 * "gyro_temp.json" (the temp region's filename below) is a REAL stock
 * filename, not an invented placeholder: `strings -a` on
 * sensors.mediatek.V2.0.so hits it in the same string-table region as
 * gyro_bias.json/gyro_cali.json/mag_bias.json/etc. (full list: gyro_bias,
 * gyro_cali, gyro_temp, mag_bias, mag_cali, acc_bias, acc_cali, als_cali,
 * baro_cali, ois_cali, ps_cali, sar_cali - the last four are for chips this
 * board's SCP firmware string table references but this HAL doesn't manage:
 * no OIS/proximity-via-hf_manager/barometer per F4294's 19-sensor list, and
 * SAR is a separate non-AIDL sensor_type). The stock blob's `[MPEKlib]`
 * import table (`Acc_init_calibration`, `Acc_get_calibration_parameter`,
 * `Acc_run_calibration`, matching `Gyro_*`, all resolved from
 * libksensor.so) confirms accel/gyro calibration runs through a real
 * self-calibration library; **no `Mag_*` or `Als_*` symbols exist anywhere
 * in the four stock binaries**, consistent with mag/light being simple
 * pass-through offset/scale values rather than a live algorithm. Disassembly
 * of the calibration setter functions shows plain register-width copies
 * (no float conversion, no scaling instructions) from the parsed JSON ints
 * into the payload - i.e. **the JSON integers are copied verbatim into the
 * wire payload, no unit transform** - and `MtkInterface::accInitCalibration
 * ()` (always called at HAL startup) zero-fills its stack buffer before the
 * fallback `Acc_init_calibration()` call used when no JSON file exists,
 * confirming "missing file -> region pushes as zero" is stock behavior, not
 * just this HAL's guess.
 *
 * See SENSORS-HAL-CALIBRATION-1309 for the full writeup: the JSON
 * <->payload field mapping per sensor, the gyro temp region's best-effort
 * field semantics (medium confidence - a thermo_a/thermo_b linear thermal
 * model per libksensor.so's exported globals, not confirmed against a real
 * gyro_temp.json sample), and the reverse-path subscribe requirement
 * (HfManagerClient::requestBiasReports/requestCaliReports/
 * requestTempReports - hf_manager silently drops BIAS_ACTION/CALI_ACTION/
 * TEMP_ACTION for a client's fd unless that client opted in via
 * HF_MANAGER_REQUEST_BIAS_DATA/CALI_DATA/_TEMP_DATA, hf_manager.c:557-600).
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
