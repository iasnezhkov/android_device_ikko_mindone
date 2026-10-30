/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Edge.h"
#include "Conversions.h"
#include "Validation.h"
#include "generated/RadioConfig.h"
#include "generated/RadioData.h"
#include "generated/RadioMessaging.h"
#include "generated/RadioModem.h"
#include "generated/RadioNetwork.h"
#include "generated/RadioSim.h"
#include "generated/RadioVoice.h"
#include <android-base/logging.h>
#include <set>

namespace mindone::radio {
struct RequestStorage {
    mr_request request{};
    std::vector<mr_signal_criteria> signals;
    std::vector<mr_gsm_broadcast_sms_config_info> broadcasts;
};
bool encodeSignalCriteria(const RadioNetwork::SetSignalStrengthReportingCriteriaArguments& args, RequestStorage& out) {
    const auto& values = std::get<0>(args);
    if (values.size() > MR_MAX_ARRAY_ELEMENTS) return false;
    std::set<std::pair<int32_t, int32_t>> seen;
    out.signals.reserve(values.size());
    for (const auto& value : values) {
        const auto ran = static_cast<int32_t>(value.ran);
        if (!seen.emplace(ran, value.signalMeasurement).second ||
            !validation::signalCriteria(ran, value.signalMeasurement, value.hysteresisMs, value.hysteresisDb, value.thresholds)) return false;
        out.signals.push_back({ran, value.signalMeasurement, static_cast<uint8_t>(value.isEnabled), value.hysteresisMs,
            value.hysteresisDb, value.thresholds.empty() ? nullptr : value.thresholds.data(), static_cast<uint32_t>(value.thresholds.size())});
    }
    out.request.signal_criteria_data = out.signals.empty() ? nullptr : out.signals.data();
    out.request.signal_criteria_len = static_cast<uint32_t>(out.signals.size());
    return true;
}
bool encodeLinkCriteria(const RadioNetwork::SetLinkCapacityReportingCriteriaArguments& args, RequestStorage& out) {
    const auto& [time, down, up, dl, ul, access] = args;
    const auto ran = static_cast<int32_t>(access);
    if (time < 0 || !validation::ran(ran) ||
        !validation::thresholds(dl, down, 0, INT32_MAX) || !validation::thresholds(ul, up, 0, INT32_MAX)) return false;
    out.request.link_criteria = {ran, time, down, up, dl.empty() ? nullptr : dl.data(), static_cast<uint32_t>(dl.size()),
        ul.empty() ? nullptr : ul.data(), static_cast<uint32_t>(ul.size())};
    return true;
}
bool encodePower(const RadioModem::SetRadioPowerArguments& args, RequestStorage& out) {
    out.request.power_on = std::get<0>(args);
    out.request.for_emergency_call = out.request.power_on && std::get<1>(args);
    out.request.preferred_for_emergency_call = out.request.for_emergency_call && std::get<2>(args);
    return true;
}
bool encodeDeviceState(const RadioModem::SendDeviceStateArguments& args, RequestStorage& out) {
    const auto type = static_cast<int32_t>(std::get<0>(args));
    if (type < 0 || type > 2) return false;
    out.request.device_state_type = type;
    out.request.boolean = std::get<1>(args);
    return true;
}
bool encodeBroadcast(const RadioMessaging::SetGsmBroadcastConfigArguments& args, RequestStorage& out) {
    const auto& values = std::get<0>(args);
    if (values.size() > MR_MAX_ARRAY_ELEMENTS) return false;
    for (const auto& value : values) {
        if (value.fromServiceId < 0 || value.fromServiceId > 65535 || value.toServiceId < 0 || value.toServiceId > 65535 ||
            value.fromCodeScheme < 0 || value.fromCodeScheme > 255 || value.toCodeScheme < 0 || value.toCodeScheme > 255) continue;
        if (value.fromServiceId > value.toServiceId || value.fromCodeScheme > value.toCodeScheme) return false;
        out.broadcasts.push_back({value.fromServiceId, value.toServiceId, value.fromCodeScheme, value.toCodeScheme, static_cast<uint8_t>(value.selected)});
    }
    out.request.broadcast_config_data = out.broadcasts.empty() ? nullptr : out.broadcasts.data();
    out.request.broadcast_config_len = static_cast<uint32_t>(out.broadcasts.size());
    return true;
}
bool encodeImsiEncryption(const RadioSim::SetCarrierInfoForImsiEncryptionArguments& args, RequestStorage&) {
    const auto& value = std::get<0>(args);
    return validation::digits(value.mcc, 3, 3) && validation::digits(value.mnc, 2, 3) &&
        !value.carrierKey.empty() && value.carrierKey.size() <= MR_MAX_ARRAY_ELEMENTS &&
        value.keyIdentifier.size() <= MR_MAX_INPUT_BYTES && value.expirationTime >= 0 && (value.keyType == 1 || value.keyType == 2);
}
bool encodeDataProfiles(const RadioData::SetDataProfileArguments& args, RequestStorage& out) {
    const auto& profiles = std::get<0>(args);
    if (profiles.size() > MR_MAX_ARRAY_ELEMENTS) return false;
    for (const auto& value : profiles) {
        const int protocol = static_cast<int32_t>(value.protocol);
        const int roaming = static_cast<int32_t>(value.roamingProtocol);
        const int auth = static_cast<int32_t>(value.authType);
        if (protocol < -1 || protocol > 5 || roaming < -1 || roaming > 5 || auth < 0 || auth > 3 ||
            value.type < 0 || value.type > 2 || value.maxConnsTime < 0 || value.maxConns < 0 || value.waitTime < 0 ||
            value.mtuV4 < 0 || value.mtuV6 < 0 || (value.persistent && value.profileId == -1)) return false;
    }
    out.request.profile_count = profiles.size();
    return true;
}
#include "generated/Routes.inc"
}
