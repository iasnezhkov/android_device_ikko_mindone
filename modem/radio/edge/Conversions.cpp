/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Conversions.h"
#include "Validation.h"
#include <aidl/android/hardware/radio/network/CellIdentityGsm.h>
#include <aidl/android/hardware/radio/network/CellIdentityWcdma.h>
#include <aidl/android/hardware/radio/network/CellIdentityLte.h>
#include <aidl/android/hardware/radio/network/CellInfoGsm.h>
#include <aidl/android/hardware/radio/network/CellInfoWcdma.h>
#include <aidl/android/hardware/radio/network/CellInfoLte.h>
#include <algorithm>
#include <limits>

namespace mindone::radio::convert {
namespace n = aidlradio::network;
constexpr int32_t unknown = INT32_MAX;
bool text(const mr_string& input, std::string& out) {
    if (input.len > MR_MAX_INPUT_BYTES || (input.len && !input.data)) return false;
    if (!input.len) { out.clear(); return true; }
    out.assign(reinterpret_cast<const char*>(input.data), input.len);
    return true;
}
bool boolean(uint8_t input, bool& out) { if (input > 1) return false; out = input != 0; return true; }
bool selection(int32_t input, bool& out) { if (input < 0 || input > 1) return false; out = input == MR_SELECTION_MODE_MANUAL; return true; }
bool technology(int32_t input, aidlradio::RadioTechnology& out) {
    if (input < 0 || input > 20) return false;
    out = static_cast<aidlradio::RadioTechnology>(input);
    return true;
}
template <typename T> bool span(const T* pointer, uint32_t length) { return length <= MR_MAX_ARRAY_ELEMENTS && (!length || pointer); }
bool plmn(const mr_string& input, std::string& mcc, std::string& mnc) {
    std::string value;
    if (!text(input, value) || !validation::plmn(value)) return false;
    if (!value.empty()) { mcc = value.substr(0, 3); mnc = value.substr(3); }
    return true;
}
bool identity(const mr_cell_identity& input, n::CellIdentity& out) {
    if (input.kind == MR_CELL_GSM) {
        n::CellIdentityGsm value;
        if (!plmn(input.plmn, value.mcc, value.mnc) || input.area > 65535 || input.cell > 65535) return false;
        value.lac = input.area; value.cid = input.cell;
        value.arfcn = unknown; value.bsic = static_cast<int8_t>(-1);
        value.operatorNames.status = n::OperatorInfo::STATUS_UNKNOWN;
        out.set<n::CellIdentity::Tag::gsm>(std::move(value));
    } else if (input.kind == MR_CELL_WCDMA) {
        n::CellIdentityWcdma value;
        if (!plmn(input.plmn, value.mcc, value.mnc) || input.area > 65535 || input.cell > 268435455) return false;
        value.lac = input.area; value.cid = input.cell;
        value.psc = unknown; value.uarfcn = unknown;
        value.operatorNames.status = n::OperatorInfo::STATUS_UNKNOWN;
        value.csgInfo = std::nullopt;
        out.set<n::CellIdentity::Tag::wcdma>(std::move(value));
    } else if (input.kind == MR_CELL_LTE) {
        n::CellIdentityLte value;
        if (!plmn(input.plmn, value.mcc, value.mnc) || input.area > 65535 || input.cell > 268435455) return false;
        value.tac = input.area; value.ci = input.cell;
        value.pci = unknown; value.earfcn = unknown; value.bandwidth = unknown;
        value.operatorNames.status = n::OperatorInfo::STATUS_UNKNOWN;
        value.csgInfo = std::nullopt;
        out.set<n::CellIdentity::Tag::lte>(std::move(value));
    } else return false;
    return true;
}
bool registration(const mr_reg_state_result& input, n::RegStateResult& out) {
    if (input.reg_state < 0 || input.reg_state > 5 || input.cell_identity_present > 1 ||
        !technology(input.rat, out.rat) || !text(input.registered_plmn, out.registeredPlmn) || !validation::plmn(out.registeredPlmn)) return false;
    if ((input.reg_state == MR_REG_STATE_HOME || input.reg_state == MR_REG_STATE_ROAMING) && input.rat == MR_RAT_UNKNOWN) return false;
    out.regState = static_cast<n::RegState>(input.reg_state);
    out.reasonForDenial = static_cast<n::RegistrationFailCause>(input.reason_for_denial);
    out.cellIdentity.set<n::CellIdentity::Tag::noinit>(false);
    out.accessTechnologySpecificInfo.set<n::AccessTechnologySpecificInfo::Tag::noinit>(false);
    return !input.cell_identity_present || identity(input.cell_identity, out.cellIdentity);
}
bool cells(const mr_value& input, std::vector<n::CellInfo>& out) {
    if (!span(input.cells_data, input.cells_len)) return false;
    out.clear(); out.reserve(input.cells_len);
    for (uint32_t index = 0; index < input.cells_len; ++index) {
        const auto& cell = input.cells_data[index];
        n::CellInfo value;
        n::CellIdentity id;
        n::SignalStrength strength;
        if (!boolean(cell.registered, value.registered) || !identity(cell.identity, id) || !signal(cell.signal, strength)) return false;
        value.connectionStatus = static_cast<n::CellConnectionStatus>(unknown);
        if (cell.identity.kind == MR_CELL_GSM) {
            n::CellInfoGsm item; item.cellIdentityGsm = id.get<n::CellIdentity::Tag::gsm>(); item.signalStrengthGsm = strength.gsm;
            value.ratSpecificInfo.set<n::CellInfoRatSpecificInfo::Tag::gsm>(std::move(item));
        } else if (cell.identity.kind == MR_CELL_WCDMA) {
            n::CellInfoWcdma item; item.cellIdentityWcdma = id.get<n::CellIdentity::Tag::wcdma>(); item.signalStrengthWcdma = strength.wcdma;
            value.ratSpecificInfo.set<n::CellInfoRatSpecificInfo::Tag::wcdma>(std::move(item));
        } else {
            n::CellInfoLte item; item.cellIdentityLte = id.get<n::CellIdentity::Tag::lte>(); item.signalStrengthLte = strength.lte;
            value.ratSpecificInfo.set<n::CellInfoRatSpecificInfo::Tag::lte>(std::move(item));
        }
        out.push_back(std::move(value));
    }
    return true;
}
bool signal(const mr_signal_strength& input, aidlradio::network::SignalStrength& result) {
    constexpr auto unknown = std::numeric_limits<int32_t>::max();
    result.gsm.signalStrength = unknown;
    result.gsm.bitErrorRate = unknown;
    result.gsm.timingAdvance = unknown;
    result.cdma.dbm = unknown;
    result.cdma.ecio = unknown;
    result.evdo.dbm = unknown;
    result.evdo.ecio = unknown;
    result.evdo.signalNoiseRatio = unknown;
    result.lte.signalStrength = unknown;
    result.lte.rsrp = unknown;
    result.lte.rsrq = unknown;
    result.lte.rssnr = unknown;
    result.lte.cqi = unknown;
    result.lte.timingAdvance = unknown;
    result.lte.cqiTableIndex = unknown;
    result.tdscdma.signalStrength = unknown;
    result.tdscdma.bitErrorRate = unknown;
    result.tdscdma.rscp = unknown;
    result.wcdma.signalStrength = unknown;
    result.wcdma.bitErrorRate = unknown;
    result.wcdma.rscp = unknown;
    result.wcdma.ecno = unknown;
    result.nr.ssRsrp = unknown;
    result.nr.ssRsrq = unknown;
    result.nr.ssSinr = unknown;
    result.nr.csiRsrp = unknown;
    result.nr.csiRsrq = unknown;
    result.nr.csiSinr = unknown;
    result.nr.csiCqiTableIndex = unknown;
    result.nr.timingAdvance = unknown;
    if (input.gsm_present) {
        if (input.gsm.signal_strength_present) result.gsm.signalStrength = input.gsm.signal_strength;
        if (input.gsm.bit_error_rate_present) result.gsm.bitErrorRate = input.gsm.bit_error_rate;
    }
    if (input.lte_present) {
        if (input.lte.rsrp_present) result.lte.rsrp = input.lte.rsrp;
        if (input.lte.rsrq_present) result.lte.rsrq = input.lte.rsrq;
        if (input.lte.rssnr_present) result.lte.rssnr = input.lte.rssnr;
    }
    if (input.wcdma_present) {
        if (input.wcdma.signal_strength_present) result.wcdma.signalStrength = input.wcdma.signal_strength;
        if (input.wcdma.rscp_present) result.wcdma.rscp = input.wcdma.rscp;
        if (input.wcdma.ecno_present) result.wcdma.ecno = input.wcdma.ecno;
    }
    return true;
}


bool imei(const mr_imei_info& input, std::optional<aidlradio::modem::ImeiInfo>& out) {
    if (input.kind < 0 || input.kind > 2) return false;
    aidlradio::modem::ImeiInfo value;
    value.type = static_cast<decltype(value.type)>(input.kind);
    if (!text(input.imei, value.imei) || !text(input.svn, value.svn)) return false;
    out = std::move(value);
    return true;
}
bool activity(const mr_activity_stats_info& input, aidlradio::modem::ActivityStatsInfo& out) {
    if (input.sleep_mode_time_ms < 0 || input.idle_mode_time_ms < 0 || !span(input.tech_specific_info_data, input.tech_specific_info_len)) return false;
    out.sleepModeTimeMs = input.sleep_mode_time_ms; out.idleModeTimeMs = input.idle_mode_time_ms;
    out.techSpecificInfo.clear();
    for (uint32_t index = 0; index < input.tech_specific_info_len; ++index) {
        const auto& entry = input.tech_specific_info_data[index];
        aidlradio::modem::ActivityStatsTechSpecificInfo value;
        if (entry.rat < 0 || entry.rat > 6 || entry.frequency_range < 0 || entry.frequency_range > 4 || entry.rx_mode_time_ms < 0) return false;
        value.rat = static_cast<decltype(value.rat)>(entry.rat); value.frequencyRange = entry.frequency_range;
        value.txmModetimeMs.assign(std::begin(entry.txm_mode_time_ms), std::end(entry.txm_mode_time_ms));
        if (std::any_of(value.txmModetimeMs.begin(), value.txmModetimeMs.end(), [](int v) { return v < 0; })) return false;
        value.rxModeTimeMs = entry.rx_mode_time_ms;
        out.techSpecificInfo.push_back(std::move(value));
    }
    return true;
}
bool capability(const mr_radio_capability& input, aidlradio::modem::RadioCapability& out) {
    if (input.phase < 0 || input.phase > 4 || input.status < 0 || input.status > 2 || !text(input.logical_modem_uuid, out.logicalModemUuid) || out.logicalModemUuid.size() > 64) return false;
    out.session = input.session; out.phase = input.phase; out.raf = input.raf; out.status = input.status;
    return true;
}
bool card(const mr_card_status& input, aidlradio::sim::CardStatus& out) {
    if (input.card_state < 0 || input.card_state > 3 || input.universal_pin_state < 0 || input.universal_pin_state > 5 ||
        !span(input.applications_data, input.applications_len) || input.applications_len > 8 ||
        input.slot_map.physical_slot_id < 0 || input.slot_map.port_id < 0 || input.supported_mep_mode < 0 || input.supported_mep_mode > 3) return false;
    const auto indexValid = [&input](int32_t index) { return index == -1 || (index >= 0 && uint32_t(index) < input.applications_len); };
    if (!indexValid(input.gsm_umts_subscription_app_index) || !indexValid(input.cdma_subscription_app_index) || !indexValid(input.ims_subscription_app_index)) return false;
    out.cardState = input.card_state;
    out.universalPinState = static_cast<decltype(out.universalPinState)>(input.universal_pin_state);
    out.gsmUmtsSubscriptionAppIndex = input.gsm_umts_subscription_app_index;
    out.cdmaSubscriptionAppIndex = input.cdma_subscription_app_index;
    out.imsSubscriptionAppIndex = input.ims_subscription_app_index;
    if (!text(input.atr, out.atr) || !text(input.iccid, out.iccid) || !text(input.eid, out.eid)) return false;
    out.slotMap.physicalSlotId = input.slot_map.physical_slot_id; out.slotMap.portId = input.slot_map.port_id;
    out.supportedMepMode = static_cast<decltype(out.supportedMepMode)>(input.supported_mep_mode);
    out.applications.clear();
    for (uint32_t index = 0; index < input.applications_len; ++index) {
        const auto& app = input.applications_data[index];
        if (app.app_type < 0 || app.app_type > 5 || app.app_state < 0 || app.app_state > 5 || app.perso_substate < 0 || app.perso_substate > 34 || app.pin1 < 0 || app.pin1 > 5 || app.pin2 < 0 || app.pin2 > 5) return false;
        aidlradio::sim::AppStatus value;
        value.appType = app.app_type; value.appState = app.app_state;
        value.persoSubstate = static_cast<decltype(value.persoSubstate)>(app.perso_substate);
        value.pin1 = static_cast<decltype(value.pin1)>(app.pin1); value.pin2 = static_cast<decltype(value.pin2)>(app.pin2);
        if (!text(app.aid_ptr, value.aidPtr) || !text(app.app_label_ptr, value.appLabelPtr) || !boolean(app.pin1_replaced, value.pin1Replaced)) return false;
        out.applications.push_back(std::move(value));
    }
    return true;
}
bool slots(const mr_value& input, std::vector<aidlradio::config::SimSlotStatus>& out) {
    if (!span(input.slots_data, input.slots_len)) return false;
    out.clear();
    for (uint32_t index = 0; index < input.slots_len; ++index) {
        const auto& slot = input.slots_data[index];
        aidlradio::config::SimSlotStatus value;
        if (slot.card_state < 0 || slot.card_state > 3 || !slot.port_info_len || !span(slot.port_info_data, slot.port_info_len) ||
            slot.supported_mep_mode < 0 || slot.supported_mep_mode > 3 || !text(slot.atr, value.atr) || !text(slot.eid, value.eid)) return false;
        value.cardState = slot.card_state;
        value.supportedMepMode = static_cast<decltype(value.supportedMepMode)>(slot.supported_mep_mode);
        for (uint32_t port = 0; port < slot.port_info_len; ++port) {
            const auto& item = slot.port_info_data[port];
            aidlradio::config::SimPortInfo info;
            if (!text(item.icc_id, info.iccId) || !boolean(item.port_active, info.portActive) || (info.portActive && item.logical_slot_id < 0)) return false;
            info.logicalSlotId = item.logical_slot_id;
            value.portInfo.push_back(std::move(info));
        }
        out.push_back(std::move(value));
    }
    return true;
}
bool phone(const mr_phone_capability& input, aidlradio::config::PhoneCapability& out) {
    if (input.max_active_data < -1 || input.max_active_internet_data < -1 || input.max_active_voice < -1 || !span(input.logical_modem_ids_data, input.logical_modem_ids_len) || !boolean(input.is_internet_lingering_supported, out.isInternetLingeringSupported)) return false;
    out.maxActiveData = input.max_active_data; out.maxActiveInternetData = input.max_active_internet_data; out.maxActiveVoice = input.max_active_voice;
    out.logicalModemIds.clear();
    for (uint32_t i = 0; i < input.logical_modem_ids_len; ++i) {
        if (input.logical_modem_ids_data[i] < 0) return false;
        out.logicalModemIds.push_back(static_cast<uint8_t>(input.logical_modem_ids_data[i]));
    }
    return true;
}
bool calls(const mr_value& input, std::vector<aidlradio::voice::Call>& out) {
    if (!span(input.calls_data, input.calls_len)) return false;
    out.clear();
    for (uint32_t index = 0; index < input.calls_len; ++index) {
        const auto& call = input.calls_data[index];
        aidlradio::voice::Call value;
        value.state = call.state; value.index = call.index; value.toa = call.toa; value.als = call.als;
        value.numberPresentation = call.number_presentation; value.namePresentation = call.name_presentation;
        value.audioQuality = static_cast<decltype(value.audioQuality)>(call.audio_quality);
        if (!boolean(call.is_mpty, value.isMpty) || !boolean(call.is_mt, value.isMT) || !boolean(call.is_voice, value.isVoice) || !boolean(call.is_voice_privacy, value.isVoicePrivacy) || !text(call.number, value.number) || !text(call.name, value.name) || !text(call.forwarded_number, value.forwardedNumber) || !span(call.uus_info_data, call.uus_info_len)) return false;
        for (uint32_t i = 0; i < call.uus_info_len; ++i) {
            const auto& entry = call.uus_info_data[i];
            aidlradio::voice::UusInfo uus;
            uus.uusType = entry.uus_type; uus.uusDcs = entry.uus_dcs;
            if (!text(entry.uus_data, uus.uusData)) return false;
            value.uusInfo.push_back(std::move(uus));
        }
        out.push_back(std::move(value));
    }
    return true;
}
}
