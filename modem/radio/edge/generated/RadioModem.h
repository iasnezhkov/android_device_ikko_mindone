/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include "Edge.h"
#include "Methods.h"
#include <optional>
#include <tuple>
#include <vector>
#include <string>
#include <aidl/android/hardware/radio/modem/BnRadioModem.h>
#include <aidl/android/hardware/radio/modem/DeviceStateType.h>
#include <aidl/android/hardware/radio/modem/IRadioModemIndication.h>
#include <aidl/android/hardware/radio/modem/IRadioModemResponse.h>
#include <aidl/android/hardware/radio/modem/NvItem.h>
#include <aidl/android/hardware/radio/modem/NvWriteItem.h>
#include <aidl/android/hardware/radio/modem/RadioCapability.h>
#include <aidl/android/hardware/radio/modem/ResetNvType.h>
namespace mindone::radio {
class RadioModem final : public ::aidl::android::hardware::radio::modem::BnRadioModem {
public:
    explicit RadioModem(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus enableModem(int32_t p_serial, bool p_on) override;
    using EnableModemArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus getBasebandVersion(int32_t p_serial) override;
    using GetBasebandVersionArguments = std::tuple<>;
    ::ndk::ScopedAStatus getDeviceIdentity(int32_t p_serial) override;
    using GetDeviceIdentityArguments = std::tuple<>;
    ::ndk::ScopedAStatus getHardwareConfig(int32_t p_serial) override;
    using GetHardwareConfigArguments = std::tuple<>;
    ::ndk::ScopedAStatus getModemActivityInfo(int32_t p_serial) override;
    using GetModemActivityInfoArguments = std::tuple<>;
    ::ndk::ScopedAStatus getModemStackStatus(int32_t p_serial) override;
    using GetModemStackStatusArguments = std::tuple<>;
    ::ndk::ScopedAStatus getRadioCapability(int32_t p_serial) override;
    using GetRadioCapabilityArguments = std::tuple<>;
    ::ndk::ScopedAStatus nvReadItem(int32_t p_serial, ::aidl::android::hardware::radio::modem::NvItem p_itemId) override;
    using NvReadItemArguments = std::tuple<::aidl::android::hardware::radio::modem::NvItem>;
    ::ndk::ScopedAStatus nvResetConfig(int32_t p_serial, ::aidl::android::hardware::radio::modem::ResetNvType p_resetType) override;
    using NvResetConfigArguments = std::tuple<::aidl::android::hardware::radio::modem::ResetNvType>;
    ::ndk::ScopedAStatus nvWriteCdmaPrl(int32_t p_serial, const std::vector<uint8_t>& p_prl) override;
    using NvWriteCdmaPrlArguments = std::tuple<std::vector<uint8_t>>;
    ::ndk::ScopedAStatus nvWriteItem(int32_t p_serial, const ::aidl::android::hardware::radio::modem::NvWriteItem& p_item) override;
    using NvWriteItemArguments = std::tuple<::aidl::android::hardware::radio::modem::NvWriteItem>;
    ::ndk::ScopedAStatus requestShutdown(int32_t p_serial) override;
    using RequestShutdownArguments = std::tuple<>;
    ::ndk::ScopedAStatus responseAcknowledgement() override;
    ::ndk::ScopedAStatus sendDeviceState(int32_t p_serial, ::aidl::android::hardware::radio::modem::DeviceStateType p_deviceStateType, bool p_state) override;
    using SendDeviceStateArguments = std::tuple<::aidl::android::hardware::radio::modem::DeviceStateType, bool>;
    ::ndk::ScopedAStatus setRadioCapability(int32_t p_serial, const ::aidl::android::hardware::radio::modem::RadioCapability& p_rc) override;
    using SetRadioCapabilityArguments = std::tuple<::aidl::android::hardware::radio::modem::RadioCapability>;
    ::ndk::ScopedAStatus setRadioPower(int32_t p_serial, bool p_powerOn, bool p_forEmergencyCall, bool p_preferredForEmergencyCall) override;
    using SetRadioPowerArguments = std::tuple<bool, bool, bool>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::modem::IRadioModemResponse>& p_radioModemResponse, const std::shared_ptr<::aidl::android::hardware::radio::modem::IRadioModemIndication>& p_radioModemIndication) override;
    ::ndk::ScopedAStatus getImei(int32_t p_serial) override;
    using GetImeiArguments = std::tuple<>;
private:
    std::shared_ptr<Edge> edge_;
};
}
