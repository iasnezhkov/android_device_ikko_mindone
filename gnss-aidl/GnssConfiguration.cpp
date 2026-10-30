/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GnssConfiguration.h"

#include <limits>
#include <utility>

#include "ExtensionFactory.h"

namespace mindone::gnss {

GnssConfiguration::GnssConfiguration(::android::sp<H::V2_1::IGnssConfiguration> backend)
    : mBackend(std::move(backend)) {}

ndk::ScopedAStatus GnssConfiguration::setSuplVersion(int32_t version) {
    if (version < 0 || version > 0xffffff) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] { return mBackend->setSuplVersion(static_cast<uint32_t>(version)); });
}

ndk::ScopedAStatus GnssConfiguration::setSuplMode(int32_t mode) {
    if (mode < 0 || (mode & ~(SUPL_MODE_MSB | SUPL_MODE_MSA)) != 0) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] { return mBackend->setSuplMode(static_cast<uint8_t>(mode)); });
}

ndk::ScopedAStatus GnssConfiguration::setLppProfile(int32_t profile) {
    if (profile < 0 || (profile & ~(LPP_PROFILE_USER_PLANE | LPP_PROFILE_CONTROL_PLANE)) != 0) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] { return mBackend->setLppProfile(static_cast<uint8_t>(profile)); });
}

ndk::ScopedAStatus GnssConfiguration::setGlonassPositioningProtocol(int32_t protocol) {
    constexpr int32_t allowed = GLONASS_POS_PROTOCOL_RRC_CPLANE | GLONASS_POS_PROTOCOL_RRLP_UPLANE |
                                GLONASS_POS_PROTOCOL_LPP_UPLANE;
    if (protocol < 0 || (protocol & ~allowed) != 0) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] {
        return mBackend->setGlonassPositioningProtocol(static_cast<uint8_t>(protocol));
    });
}

ndk::ScopedAStatus GnssConfiguration::setEmergencySuplPdn(bool enable) {
    return forward([&] { return mBackend->setEmergencySuplPdn(enable); });
}

ndk::ScopedAStatus GnssConfiguration::setEsExtensionSec(int32_t seconds) {
    if (seconds < 0) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] { return mBackend->setEsExtensionSec(static_cast<uint32_t>(seconds)); });
}

ndk::ScopedAStatus GnssConfiguration::setBlocklist(
        const std::vector<A::BlocklistedSource>& sources) {
    ::android::hardware::hidl_vec<H::V2_1::IGnssConfiguration::BlacklistedSource> converted;
    converted.resize(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        int32_t constellation = static_cast<int32_t>(source.constellation);
        if (constellation < 0 ||
            constellation > static_cast<int32_t>(A::GnssConstellationType::IRNSS) ||
            source.svid < 0 || source.svid > std::numeric_limits<int16_t>::max()) {
            return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_INVALID_ARGUMENT);
        }
        converted[i].constellation = static_cast<H::V2_0::GnssConstellationType>(constellation);
        converted[i].svid = static_cast<int16_t>(source.svid);
    }
    return forward([&] { return mBackend->setBlacklist_2_1(converted); });
}

template <>
ndk::ScopedAStatus makeExtension(const ::android::sp<H::V2_1::IGnss>& backend,
                                 std::shared_ptr<A::IGnssConfiguration>* result, bool) {
    result->reset();
    auto extension = backend->getExtensionGnssConfiguration_2_1();
    if (!extension.isOk()) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_GENERIC);
    }
    ::android::sp<H::V2_1::IGnssConfiguration> implementation = extension;
    *result = ndk::SharedRefBase::make<GnssConfiguration>(implementation);
    return ndk::ScopedAStatus::ok();
}

}
