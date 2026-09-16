/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Minimal, dependency-free reader/writer for the exact JSON shape used by
 * the stock MTK sensor calibration files under /mnt/vendor/nvcfg/sensor/:
 *
 *   {
 *     "some_key": [
 *         123,
 *         -456,
 *         ...
 *       ]
 *   }
 *
 * i.e. one object, one key, one array of (possibly negative) decimal
 * integers - confirmed against the real on-device dump (the on-device nvcfg dump, 
 * cali/nvcfg-sensor.txt, 13.09): gyro_bias.json, mag_bias.json, mag_cali.json
 * all match this grammar verbatim, including irregular indentation (6 vs 4
 * spaces) that a strict/pretty-printer-only reader might choke on.
 *
 * This is intentionally NOT a general JSON parser (no nesting, no strings-
 * as-values, no floats) and intentionally has NO Android/Linux-only
 * dependencies (no <log/log.h>, no Linux uapi headers) so it can be compiled
 * and unit-tested with a plain host compiler - see NvcfgJsonTest.cpp -
 * without
 * an AOSP build tree, which matters for a device-free task. CalibrationStore
 * (Android-facing, pulls in hf_manager_uapi.h/HfManagerClient.h) is a thin
 * wrapper around this file's two functions plus ALOG* logging.
 *
 * Rationale for hand-rolling instead of libjsoncpp (which IS vendor-
 * available in AOSP and would also work here): this grammar is trivial
 * enough to get exactly right in a few dozen lines, and doing so avoids
 * adding a new Soong dependency to an Android.bp that already carries a
 * "verify AIDL version before first build" warning (see Android.bp) - one
 * less thing that can silently fail to link in a from-scratch build. If the
 * the tree prefers libjsoncpp for robustness/consistency with other
 * vendor code, swapping these two functions' bodies for
 * Json::CharReaderBuilder / Json::StreamWriterBuilder is a self-contained,
 * low-risk change - the CalibrationStore call sites do not need to change.
 */

#ifndef MINDONE_NVCFG_JSON_H_
#define MINDONE_NVCFG_JSON_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mindone {
namespace hf {
namespace nvcfg_json {

// Reads `path`, expecting the single-key/int-array shape documented above.
// Returns std::nullopt if the file does not exist (the common case - most
// calibration files are simply absent when factory cal was never run for
// that sensor, e.g. acc_cali.json/als_cali.json on our device today) or is
// malformed. If the object's key does not match `expectedKey`, the values
// are still returned (a WARN-level condition for the caller to log) since
// tolerating a key-name mismatch is safer than refusing real calibration
// data over a cosmetic naming difference between firmware revisions.
//
// `outKeyMismatch`, if non-null, is set to true when the file parsed fine
// but its key differs from `expectedKey` (so the Android-facing caller can
// ALOGW with the real key name; this header has no logging of its own).
std::optional<std::vector<int32_t>> readIntArray(const std::string& path,
                                                  const std::string& expectedKey,
                                                  bool* outKeyMismatch = nullptr,
                                                  std::string* outActualKey = nullptr);

// Writes `path` atomically (write to `path`+".tmp", fsync, rename() over
// `path`, best-effort fsync of the containing directory) so a power loss
// mid-write can never leave a torn/partial JSON file on the nvcfg partition.
// Formats output in the same style as the stock files (2-space object
// indent, 6-space array-element indent) purely so a human `cat`ing the file
// later sees something familiar - the reader above does not care about
// whitespace at all.
bool writeIntArray(const std::string& path, const std::string& key,
                    const std::vector<int32_t>& values);

// Split form of the above for a caller that needs a step between "temp file
// fully written" and "renamed over the real path". Returns the temp file's
// path on success (already fsync()'d, not yet renamed), or nullopt on any
// failure (temp file already cleaned up in that case). The temp file is
// created with mode 0600, the same mode the stock files carry
// (-rw------- system:system, the on-device nvcfg-sensor dump).
std::optional<std::string> writeIntArrayTmp(const std::string& path, const std::string& key,
                                             const std::vector<int32_t>& values);

// Second half of the split above: rename()s `tmpPath` over `path` and
// best-effort fsyncs the containing directory. `writeIntArray()` is exactly
// `writeIntArrayTmp()` followed by this, with nothing in between.
bool commitAtomicWrite(const std::string& tmpPath, const std::string& path);

}  // namespace nvcfg_json
}  // namespace hf
}  // namespace mindone

#endif  // MINDONE_NVCFG_JSON_H_
