#pragma once

#include <android/hardware/camera/device/3.2/types.h>

namespace mindone {
namespace camera {

constexpr uint32_t kVendorTagSessionParamHdrMode = 0x80030001u;
constexpr bool kEnableMaximumResolutionAugment = false;

bool augmentHdrSessionKey(::android::hardware::camera::device::V3_2::CameraMetadata* metadata);

bool augmentMaximumResolution(::android::hardware::camera::device::V3_2::CameraMetadata* metadata);

}
}
