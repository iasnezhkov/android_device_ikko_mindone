/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

enum : int32_t {
    EX_NONE = 0,
    EX_ILLEGAL_ARGUMENT = -3,
    EX_ILLEGAL_STATE = -5,
    EX_UNSUPPORTED_OPERATION = -7,
};

namespace ndk {

class ScopedAStatus {
  public:
    static ScopedAStatus ok() { return ScopedAStatus(EX_NONE, 0); }
    static ScopedAStatus fromExceptionCode(int32_t code) { return ScopedAStatus(code, 0); }
    static ScopedAStatus fromServiceSpecificError(int32_t err) { return ScopedAStatus(EX_NONE, err); }

    bool isOk() const { return mException == EX_NONE && mServiceError == 0; }
    int32_t getExceptionCode() const { return mException; }
    int32_t getServiceSpecificError() const { return mServiceError; }

  private:
    ScopedAStatus(int32_t exception, int32_t serviceError)
        : mException(exception), mServiceError(serviceError) {}

    int32_t mException;
    int32_t mServiceError;
};

class SharedRefBase {
  public:
    virtual ~SharedRefBase() = default;

    template <typename T, typename... Args>
    static std::shared_ptr<T> make(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
};

}

namespace aidl::android::media::audio::common {

struct Boolean {
    bool value = false;
    bool operator==(const Boolean&) const = default;
};

struct Int {
    int32_t value = 0;
    bool operator==(const Int&) const = default;
};

struct Float {
    float value = 0.0f;
    bool operator==(const Float&) const = default;
};

}

namespace aidl::android::hardware::audio::core {

class IBluetooth {
  public:
    struct ScoConfig {
        enum class Mode : int8_t {
            UNSPECIFIED = 0,
            SCO = 1,
            SCO_WB = 2,
            SCO_SWB = 3,
        };

        std::optional<::aidl::android::media::audio::common::Boolean> isEnabled;
        std::optional<::aidl::android::media::audio::common::Boolean> isNrecEnabled;
        Mode mode = Mode::UNSPECIFIED;
        std::optional<std::string> debugName;
    };

    struct HfpConfig {
        static constexpr float VOLUME_MIN = 0.0f;
        static constexpr float VOLUME_MAX = 1.0f;

        std::optional<::aidl::android::media::audio::common::Boolean> isEnabled;
        std::optional<::aidl::android::media::audio::common::Int> sampleRate;
        std::optional<::aidl::android::media::audio::common::Float> volume;
    };

    virtual ~IBluetooth() = default;
    virtual ndk::ScopedAStatus setScoConfig(const ScoConfig& config, ScoConfig* result) = 0;
    virtual ndk::ScopedAStatus setHfpConfig(const HfpConfig& config, HfpConfig* result) = 0;
};

class BnBluetooth : public IBluetooth, public ndk::SharedRefBase {};

}
