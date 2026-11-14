/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <system/camera_metadata.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace mindone::camera {

using Metadata = std::unique_ptr<camera_metadata_t, decltype(&free_camera_metadata)>;

template <class T>
std::vector<T> values(const camera_metadata_t* metadata, uint32_t tag) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(metadata, tag, &entry) != 0) return {};
    const auto* data = reinterpret_cast<const T*>(entry.data.u8);
    return std::vector<T>(data, data + entry.count);
}

inline bool store(camera_metadata_t* metadata, uint32_t tag, const void* data, size_t count) {
    camera_metadata_entry_t entry;
    if (find_camera_metadata_entry(metadata, tag, &entry) == 0) {
        if (count == 0) return delete_camera_metadata_entry(metadata, entry.index) == 0;
        return update_camera_metadata_entry(metadata, entry.index, data, count, nullptr) == 0;
    }
    return count == 0 || add_camera_metadata_entry(metadata, tag, data, count) == 0;
}

inline std::vector<uint64_t> aligned(const std::vector<uint8_t>& bytes) {
    std::vector<uint64_t> copy((bytes.size() + 7) / 8);
    std::memcpy(copy.data(), bytes.data(), bytes.size());
    return copy;
}

template <class T>
std::vector<T> peek(const std::vector<uint8_t>& bytes, uint32_t tag) {
    if (bytes.empty()) return {};
    auto copy = aligned(bytes);
    const auto* metadata = reinterpret_cast<const camera_metadata_t*>(copy.data());
    size_t size = bytes.size();
    if (validate_camera_metadata_structure(metadata, &size) != 0) return {};
    return values<T>(metadata, tag);
}

template <class Edit>
bool editMetadata(std::vector<uint8_t>* bytes, size_t extraEntries, size_t extraData, Edit edit) {
    if (bytes->empty()) return false;
    auto copy = aligned(*bytes);
    const auto* source = reinterpret_cast<const camera_metadata_t*>(copy.data());
    size_t size = bytes->size();
    if (validate_camera_metadata_structure(source, &size) != 0) return false;
    Metadata metadata(allocate_camera_metadata(get_camera_metadata_entry_count(source) + extraEntries,
                                               get_camera_metadata_data_count(source) + extraData),
                      free_camera_metadata);
    if (!metadata || append_camera_metadata(metadata.get(), source) != 0) return false;
    if (!edit(metadata.get())) return false;
    Metadata compact(clone_camera_metadata(metadata.get()), free_camera_metadata);
    if (!compact) return false;
    const auto* raw = reinterpret_cast<const uint8_t*>(compact.get());
    bytes->assign(raw, raw + get_camera_metadata_size(compact.get()));
    return true;
}

}
