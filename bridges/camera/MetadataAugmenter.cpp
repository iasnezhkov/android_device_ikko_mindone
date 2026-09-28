#include "MetadataAugmenter.h"

#include <log/log.h>
#include <system/camera_metadata.h>

#include <cstring>
#include <vector>

namespace mindone {
namespace camera {

using ::android::hardware::camera::device::V3_2::CameraMetadata;

bool augmentHdrSessionKey(CameraMetadata* metadata) {
    auto* src = reinterpret_cast<camera_metadata_t*>(metadata->data());
    if (src == nullptr) {
        ALOGE("mindone.camera: augmentHdrSessionKey called with an empty characteristics blob");
        return false;
    }
    camera_metadata_t* dst = allocate_camera_metadata(
            get_camera_metadata_entry_count(src) + 1,
            get_camera_metadata_data_count(src) + calculate_camera_metadata_entry_data_size(TYPE_INT32, 1));
    if (dst == nullptr) {
        ALOGE("mindone.camera: allocate_camera_metadata failed while adding the HDR session key");
        return false;
    }
    if (append_camera_metadata(dst, src) != 0) {
        ALOGE("mindone.camera: append_camera_metadata failed while adding the HDR session key");
        free_camera_metadata(dst);
        return false;
    }
    camera_metadata_entry_t entry{};
    int found = find_camera_metadata_entry(dst, ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, &entry);
    int rc;
    if (found == 0) {
        std::vector<int32_t> keys(entry.data.i32, entry.data.i32 + entry.count);
        for (int32_t key : keys) {
            if (key == static_cast<int32_t>(kVendorTagSessionParamHdrMode)) {
                free_camera_metadata(dst);
                return true;
            }
        }
        keys.push_back(static_cast<int32_t>(kVendorTagSessionParamHdrMode));
        rc = update_camera_metadata_entry(dst, entry.index, keys.data(), keys.size(), nullptr);
    } else {
        int32_t key = static_cast<int32_t>(kVendorTagSessionParamHdrMode);
        rc = add_camera_metadata_entry(dst, ANDROID_REQUEST_AVAILABLE_SESSION_KEYS, &key, 1);
    }
    if (rc != 0) {
        ALOGE("mindone.camera: could not append the HDR session key to availableSessionKeys, rc=%d", rc);
        free_camera_metadata(dst);
        return false;
    }
    size_t newSize = get_camera_metadata_size(dst);
    metadata->resize(newSize);
    memcpy(metadata->data(), dst, newSize);
    free_camera_metadata(dst);
    return true;
}

bool augmentMaximumResolution(CameraMetadata*) {
    ALOGW("mindone.camera: augmentMaximumResolution is not implemented - the maximum-resolution "
          "geometry (ANDROID_SENSOR_INFO_*_MAXIMUM_RESOLUTION, ANDROID_SCALER_AVAILABLE_STREAM_"
          "CONFIGURATIONS_MAXIMUM_RESOLUTION) has to come from a live IMX766 4-cell characteristics "
          "dump, not from guessed values; see i-yxpnata4lxwr / i-p9hh9mvcydv2");
    return false;
}

}
}
