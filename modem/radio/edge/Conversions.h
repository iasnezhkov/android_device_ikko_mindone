/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include "mindone_radio.h"
#include <aidl/android/hardware/radio/RadioTechnology.h>
#include <aidl/android/hardware/radio/network/CellInfo.h>
#include <aidl/android/hardware/radio/network/RegStateResult.h>
#include <aidl/android/hardware/radio/network/SignalStrength.h>
#include <aidl/android/hardware/radio/config/PhoneCapability.h>
#include <aidl/android/hardware/radio/config/SimSlotStatus.h>
#include <aidl/android/hardware/radio/modem/ActivityStatsInfo.h>
#include <aidl/android/hardware/radio/modem/ImeiInfo.h>
#include <aidl/android/hardware/radio/modem/RadioCapability.h>
#include <aidl/android/hardware/radio/sim/CardStatus.h>
#include <aidl/android/hardware/radio/voice/Call.h>
#include <optional>
#include <string>
#include <vector>
namespace mindone::radio::convert {
namespace aidlradio = ::aidl::android::hardware::radio;
bool text(const mr_string&, std::string&);
bool boolean(uint8_t, bool&);
bool selection(int32_t, bool&);
bool technology(int32_t, aidlradio::RadioTechnology&);
bool signal(const mr_signal_strength&, aidlradio::network::SignalStrength&);
bool registration(const mr_reg_state_result&, aidlradio::network::RegStateResult&);
bool cells(const mr_value&, std::vector<aidlradio::network::CellInfo>&);
bool imei(const mr_imei_info&, std::optional<aidlradio::modem::ImeiInfo>&);
bool activity(const mr_activity_stats_info&, aidlradio::modem::ActivityStatsInfo&);
bool capability(const mr_radio_capability&, aidlradio::modem::RadioCapability&);
bool card(const mr_card_status&, aidlradio::sim::CardStatus&);
bool slots(const mr_value&, std::vector<aidlradio::config::SimSlotStatus>&);
bool phone(const mr_phone_capability&, aidlradio::config::PhoneCapability&);
bool calls(const mr_value&, std::vector<aidlradio::voice::Call>&);
}
