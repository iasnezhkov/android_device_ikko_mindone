/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
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

std::optional<std::vector<int32_t>> readIntArray(const std::string& path,
                                                  const std::string& expectedKey,
                                                  bool* outKeyMismatch = nullptr,
                                                  std::string* outActualKey = nullptr);

bool writeIntArray(const std::string& path, const std::string& key,
                    const std::vector<int32_t>& values);

std::optional<std::string> writeIntArrayTmp(const std::string& path, const std::string& key,
                                             const std::vector<int32_t>& values);

bool commitAtomicWrite(const std::string& tmpPath, const std::string& path);

}
}
}

#endif
