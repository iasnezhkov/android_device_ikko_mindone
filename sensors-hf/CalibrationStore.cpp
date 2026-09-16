/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone.sensors.cali"

#include "CalibrationStore.h"

#include <cerrno>
#include <cstring>
#include <ctime>

#include <log/log.h>

#include "NvcfgJson.h"

namespace mindone {
namespace hf {

namespace {

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// Files are rewritten through nvcfg_json::writeIntArray() (write .tmp, fsync,
// rename). No SELinux relabel step is needed: a file created inside a
// directory labelled nvcfg_file inherits that type (SELinux default for new
// file objects is the parent directory's type unless a type_transition rule
// says otherwise; none does here), and the stock hal_sensors_default policy
// already grants create_file_perms on nvcfg_file.

bool allZero(const std::vector<int32_t>& v) {
    for (int32_t x : v) {
        if (x != 0) return false;
    }
    return true;
}

// Copies as many int32 values from `values` into `buf` at `region` as fit,
// warning (not failing) on a count mismatch - a JSON file with the "wrong"
// number of values is still real, on-device factory data, and refusing to
// use any of it in that case would be worse than a partial/zero-padded
// push. `what`/`sensorType` are for the log line only.
void writeRegion(std::vector<uint8_t>* buf, CalibrationStore::RegionLayout region,
                  const std::vector<int32_t>& values, const char* what, uint8_t sensorType) {
    const size_t wantCount = region.length / 4;
    if (values.size() != wantCount) {
        ALOGW("sensor_type=%u: %s region wants %zu int32 value(s), json had %zu - "
              "copying min(both), rest stays zero",
              sensorType, what, wantCount, values.size());
    }
    const size_t n = std::min(wantCount, values.size());
    if (n > 0) {
        std::memcpy(buf->data() + region.offset, values.data(), n * sizeof(int32_t));
    }
}

}  // namespace

// ---------------------------------------------------------------------
// The fixed layout table. See CalibrationStore.h's file header comment for
// the kernel-source citations behind every offset/length below.
// ---------------------------------------------------------------------
const std::array<CalibrationStore::SensorLayout, 4>& CalibrationStore::allLayouts() {
    static const std::array<SensorLayout, 4> kLayouts = {{
            // ACCELEROMETER: 24B = bias[3] (0..11) + cali[3] (12..23). An
            // EXPLICIT kernel case (not the "whole buffer is cali" default
            // branch), so acc_cali.json is expected to hold exactly 3
            // values, same split-file shape as gyro - see CalibrationStore.h
            // header comment. No acc_cali.json/acc_bias.json sample exists
            // on this device (no factory accel cal was ever run) to check
            // directly; buildPayload() still auto-detects a 6-value combined
            // file too, as a harmless defensive fallback.
            {SENSOR_TYPE_ACCELEROMETER, 24, {0, 12}, {12, 12}, {0, 0}, "acc", "acc_bias.json",
             "acc_bias", "acc_cali.json", "acc_cali"},
            // MAGNETIC_FIELD: 36B = bias[3] (0..11) + cali[6] (12..35).
            // Both files exist and were dumped verbatim on-device: mag_bias
            // ={90374,-4208,285690}, mag_cali={90374,-4208,285690,36368,0,0}
            // - note mag_cali's first 3 values duplicate mag_bias exactly.
            {SENSOR_TYPE_MAGNETIC_FIELD, 36, {0, 12}, {12, 24}, {0, 0}, "mag", "mag_bias.json",
             "mag_bias", "mag_cali.json", "mag_cali"},
            // GYROSCOPE: 48B = bias[3] (0..11) + cali[3] (12..23) +
            // temp[6] (24..47). gyro_bias.json exists on-device
            // ({4522,-8042,-3870}); gyro_cali.json/gyro_temp.json are real
            // filenames confirmed present in the stock blob's string table
            // (`strings -a sensors.mediatek.V2.0.so`) but no sample of
            // either exists on THIS device (no factory run for them here),
            // so those two regions push as zero until either a sample
            // appears or a runtime BIAS/CALI/TEMP_ACTION populates our own
            // persisted copy. temp's 6 int32 are best-effort read as a
            // linear thermal model (libksensor.so exports thermo_a/thermo_b/
            // mpe_update_gyro_thermal_calib): [0..2]=per-axis slope,
            // [3..5]=per-axis intercept - medium confidence, not confirmed
            // against a real sample.
            {SENSOR_TYPE_GYROSCOPE, 48, {0, 12}, {12, 12}, {24, 24}, "gyro", "gyro_bias.json",
             "gyro_bias", "gyro_cali.json", "gyro_cali"},
            // LIGHT: 4B, the WHOLE buffer is the "cali" region (no bias
            // split - see transceiver_update_config()'s default branch).
            // als_cali.json is a confirmed real filename (stock string
            // table) but no sample exists on this device.
            {SENSOR_TYPE_LIGHT, 4, {0, 0}, {0, 4}, {0, 0}, "light", nullptr, nullptr,
             "als_cali.json", "als_cali"},
    }};
    return kLayouts;
}

const CalibrationStore::SensorLayout* CalibrationStore::layoutFor(uint8_t sensorType) {
    for (const auto& layout : allLayouts()) {
        if (layout.sensorType == sensorType) return &layout;
    }
    return nullptr;
}

CalibrationStore::CalibrationStore(std::string baseDir) : baseDir_(std::move(baseDir)) {}

void CalibrationStore::seedCache(CachedRegion* cache, const std::vector<int32_t>& values) {
    // Called with mutex_ held (pushAll). lastWriteMs stays 0: a genuinely new value may be
    // persisted right away; an SCP report that merely echoes the pushed values is a no-op.
    cache->values = values;
    cache->haveValue = true;
}

std::vector<uint8_t> CalibrationStore::buildPayload(const SensorLayout& layout) {
    std::vector<uint8_t> buf(layout.totalLength, 0);  // zero default for every region

    if (layout.bias.length > 0 && layout.biasFile) {
        auto bias = nvcfg_json::readIntArray(baseDir_ + "/" + layout.biasFile, layout.biasKey);
        if (bias) {
            writeRegion(&buf, layout.bias, *bias, "bias", layout.sensorType);
            seedCache(&biasCache_[layout.sensorType], *bias);
        } else {
            ALOGI("sensor_type=%u (%s): no %s - bias region defaults to zero", layout.sensorType,
                  layout.name, layout.biasFile);
        }
    }

    if (layout.cali.length > 0 && layout.caliFile) {
        bool keyMismatch = false;
        std::string actualKey;
        auto cali = nvcfg_json::readIntArray(baseDir_ + "/" + layout.caliFile, layout.caliKey,
                                              &keyMismatch, &actualKey);
        if (!cali) {
            ALOGI("sensor_type=%u (%s): no %s - cali region defaults to zero", layout.sensorType,
                  layout.name, layout.caliFile);
        } else {
            if (keyMismatch) {
                ALOGW("sensor_type=%u (%s): %s key is \"%s\", expected \"%s\" - using values "
                      "anyway",
                      layout.sensorType, layout.name, layout.caliFile, actualKey.c_str(),
                      layout.caliKey);
            }
            const size_t biasCount = layout.bias.length / 4;
            const size_t caliCount = layout.cali.length / 4;
            if (layout.bias.length > 0 && cali->size() == biasCount + caliCount) {
                // Combined file holding [bias..., cali...] together, same
                // shape as mag_cali.json's own 6-value layout - the
                // suspected acc_cali.json case (see class-level comment).
                // This file wins over a same-named *_bias.json if both
                // happened to exist, since it's a strict superset.
                ALOGI("sensor_type=%u (%s): %s has %zu values (bias+cali count) - "
                      "treating as a combined file, first %zu -> bias, rest -> cali",
                      layout.sensorType, layout.name, layout.caliFile, cali->size(), biasCount);
                std::vector<int32_t> biasPart(cali->begin(), cali->begin() + biasCount);
                std::vector<int32_t> caliPart(cali->begin() + biasCount, cali->end());
                writeRegion(&buf, layout.bias, biasPart, "bias(from combined cali file)",
                            layout.sensorType);
                writeRegion(&buf, layout.cali, caliPart, "cali", layout.sensorType);
                seedCache(&biasCache_[layout.sensorType], biasPart);
                seedCache(&caliCache_[layout.sensorType], caliPart);
            } else {
                writeRegion(&buf, layout.cali, *cali, "cali", layout.sensorType);
                seedCache(&caliCache_[layout.sensorType], *cali);
            }
        }
    }

    // temp region: no stock filename is known for ANY sensor_type today -
    // it only ever gets populated by our own reverse-path persistence (see
    // persistRegion()), under an invented "<name>_temp.json" name. Reading
    // it back here on a later boot is exactly what makes that persistence
    // useful instead of write-only.
    if (layout.temp.length > 0) {
        const std::string tempFile = std::string(layout.name) + "_temp.json";
        auto temp = nvcfg_json::readIntArray(baseDir_ + "/" + tempFile,
                                              std::string(layout.name) + "_temp");
        if (temp) {
            writeRegion(&buf, layout.temp, *temp, "temp", layout.sensorType);
        }
    }

    return buf;
}

void CalibrationStore::pushAll(HfManagerClient& client) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& layout : allLayouts()) {
        if (!client.queryRegistered(layout.sensorType)) {
            continue;  // not present on this SCP firmware - see header comment
        }
        std::vector<uint8_t> payload = buildPayload(layout);
        if (!client.configCalibration(layout.sensorType, payload.data(),
                                       static_cast<uint8_t>(payload.size()))) {
            ALOGE("sensor_type=%u (%s): CONFIG_CALI push failed, %zu bytes", layout.sensorType,
                  layout.name, payload.size());
            continue;
        }
        ALOGI("sensor_type=%u (%s): pushed %zu-byte CONFIG_CALI", layout.sensorType, layout.name,
              payload.size());

        // Subscribe so onKernelEvent() actually receives anything -
        // hf_manager_distinguish_event() (hf_manager.c:557-600) drops BIAS_
        // ACTION/CALI_ACTION/TEMP_ACTION for this client's fd otherwise,
        // regardless of whether the sensor itself is enabled.
        if (layout.bias.length > 0) {
            if (!client.requestBiasReports(layout.sensorType, true)) {
                ALOGE("sensor_type=%u (%s): failed to subscribe to BIAS_ACTION",
                      layout.sensorType, layout.name);
            }
        }
        if (layout.cali.length > 0) {
            if (!client.requestCaliReports(layout.sensorType, true)) {
                ALOGE("sensor_type=%u (%s): failed to subscribe to CALI_ACTION",
                      layout.sensorType, layout.name);
            }
        }
        if (layout.temp.length > 0) {
            if (!client.requestTempReports(layout.sensorType, true)) {
                ALOGE("sensor_type=%u (%s): failed to subscribe to TEMP_ACTION",
                      layout.sensorType, layout.name);
            }
        }
    }
}

void CalibrationStore::onKernelEvent(const hf_manager_event& ke) {
    const SensorLayout* layout = layoutFor(ke.sensor_type);
    if (!layout) return;  // not a sensor_type this store manages

    std::lock_guard<std::mutex> lock(mutex_);
    if (ke.action == BIAS_ACTION && layout->bias.length > 0) {
        persistRegion(*layout, /*isBias=*/true, ke.word, layout->bias.length / 4);
    } else if (ke.action == CALI_ACTION && layout->cali.length > 0) {
        persistRegion(*layout, /*isBias=*/false, ke.word, layout->cali.length / 4);
    }
    // TEMP_ACTION is subscribed to (pushAll() above) but not yet persisted
    // here: the temp region's field semantics (6 int32 for gyro - per-axis
    // offset+slope? a bucketed table?) are not confirmed against real
    // hardware, unlike bias/cali which map 1:1 onto the same *_bias.json/
    // *_cali.json shape already proven by mag/gyro's real files. Wire it
    // the same way as the two branches above (a third cache + "<name>_
    // temp.json") once that's confirmed - see docs/SENSORS-HAL-
    // CALIBRATION-1309.md open item.
}

void CalibrationStore::persistRegion(const SensorLayout& layout, bool isBias,
                                      const int32_t* words, size_t count) {
    auto& cache = isBias ? biasCache_[layout.sensorType] : caliCache_[layout.sensorType];
    const std::vector<int32_t> newValues(words, words + count);
    const char* what = isBias ? "bias" : "cali";

    if (cache.haveValue && cache.values == newValues) {
        return;  // unchanged - nothing to log, nothing to write
    }
    if (allZero(newValues)) {
        // An all-zero report is the SCP's "no calibration" state, not a measured result
        // (the factory files on this device are all non-zero). Persisting it would erase
        // the factory data on disk, so keep it in RAM only and say so once per change.
        ALOGW("sensor_type=%u (%s): %s reported all-zero - not persisted (factory data kept)",
              layout.sensorType, layout.name, what);
        cache.values = newValues;
        cache.haveValue = true;
        return;
    }

    const int64_t now = nowMs();
    if (cache.haveValue && (now - cache.lastWriteMs) < kMinRewriteIntervalMs) {
        ALOGW("sensor_type=%u (%s): %s changed again within %lldms of the last nvcfg write - "
              "updating memory only, throttling the write (flash-wear guard)",
              layout.sensorType, layout.name, what,
              static_cast<long long>(kMinRewriteIntervalMs));
        cache.values = newValues;  // keep latest in RAM so a later diff is still correct
        return;
    }

    const char* file = isBias ? layout.biasFile : layout.caliFile;
    const char* key = isBias ? layout.biasKey : layout.caliKey;
    if (!file) {
        // Currently unreachable (every region with length>0 in the table
        // above has a filename), kept as a guard against a future table
        // entry that adds a region without a filename.
        ALOGW("sensor_type=%u (%s): %s updated but no nvcfg filename is known - not persisted",
              layout.sensorType, layout.name, what);
        return;
    }

    // Deliberately always writes the plain {biasKey:[...]}/{caliKey:[...]}
    // shape, never attempts to reconstruct a combined file (see buildPayload
    // ()'s auto-detected combined-file case). On a device that started with
    // a combined acc_cali.json, the FIRST runtime bias update forks it into
    // acc_bias.json + acc_cali.json - both shapes are read correctly by
    // buildPayload() on the next boot, so this is a one-time format
    // migration, not a bug.
    const std::string path = baseDir_ + "/" + file;
    if (nvcfg_json::writeIntArray(path, key, newValues)) {
        cache.values = newValues;
        cache.haveValue = true;
        cache.lastWriteMs = now;
        ALOGI("sensor_type=%u (%s): persisted runtime %s -> %s", layout.sensorType, layout.name,
              what, path.c_str());
    } else {
        ALOGE("sensor_type=%u (%s): failed to persist runtime %s to %s", layout.sensorType,
              layout.name, what, path.c_str());
    }
}

}  // namespace hf
}  // namespace mindone
