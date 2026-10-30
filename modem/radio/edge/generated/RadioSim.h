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
#include <aidl/android/hardware/radio/sim/BnRadioSim.h>
#include <aidl/android/hardware/radio/sim/CardPowerState.h>
#include <aidl/android/hardware/radio/sim/CarrierRestrictions.h>
#include <aidl/android/hardware/radio/sim/CdmaSubscriptionSource.h>
#include <aidl/android/hardware/radio/sim/IRadioSimIndication.h>
#include <aidl/android/hardware/radio/sim/IRadioSimResponse.h>
#include <aidl/android/hardware/radio/sim/IccIo.h>
#include <aidl/android/hardware/radio/sim/ImsiEncryptionInfo.h>
#include <aidl/android/hardware/radio/sim/PersoSubstate.h>
#include <aidl/android/hardware/radio/sim/PhonebookRecordInfo.h>
#include <aidl/android/hardware/radio/sim/SelectUiccSub.h>
#include <aidl/android/hardware/radio/sim/SessionInfo.h>
#include <aidl/android/hardware/radio/sim/SimApdu.h>
#include <aidl/android/hardware/radio/sim/SimLockMultiSimPolicy.h>
namespace mindone::radio {
class RadioSim final : public ::aidl::android::hardware::radio::sim::BnRadioSim {
public:
    explicit RadioSim(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus areUiccApplicationsEnabled(int32_t p_serial) override;
    using AreUiccApplicationsEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus changeIccPin2ForApp(int32_t p_serial, const std::string& p_oldPin2, const std::string& p_newPin2, const std::string& p_aid) override;
    using ChangeIccPin2ForAppArguments = std::tuple<std::string, std::string, std::string>;
    ::ndk::ScopedAStatus changeIccPinForApp(int32_t p_serial, const std::string& p_oldPin, const std::string& p_newPin, const std::string& p_aid) override;
    using ChangeIccPinForAppArguments = std::tuple<std::string, std::string, std::string>;
    ::ndk::ScopedAStatus enableUiccApplications(int32_t p_serial, bool p_enable) override;
    using EnableUiccApplicationsArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus getAllowedCarriers(int32_t p_serial) override;
    using GetAllowedCarriersArguments = std::tuple<>;
    ::ndk::ScopedAStatus getCdmaSubscription(int32_t p_serial) override;
    using GetCdmaSubscriptionArguments = std::tuple<>;
    ::ndk::ScopedAStatus getCdmaSubscriptionSource(int32_t p_serial) override;
    using GetCdmaSubscriptionSourceArguments = std::tuple<>;
    ::ndk::ScopedAStatus getFacilityLockForApp(int32_t p_serial, const std::string& p_facility, const std::string& p_password, int32_t p_serviceClass, const std::string& p_appId) override;
    using GetFacilityLockForAppArguments = std::tuple<std::string, std::string, int32_t, std::string>;
    ::ndk::ScopedAStatus getIccCardStatus(int32_t p_serial) override;
    using GetIccCardStatusArguments = std::tuple<>;
    ::ndk::ScopedAStatus getImsiForApp(int32_t p_serial, const std::string& p_aid) override;
    using GetImsiForAppArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus getSimPhonebookCapacity(int32_t p_serial) override;
    using GetSimPhonebookCapacityArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSimPhonebookRecords(int32_t p_serial) override;
    using GetSimPhonebookRecordsArguments = std::tuple<>;
    ::ndk::ScopedAStatus iccCloseLogicalChannel(int32_t p_serial, int32_t p_channelId) override;
    using IccCloseLogicalChannelArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus iccIoForApp(int32_t p_serial, const ::aidl::android::hardware::radio::sim::IccIo& p_iccIo) override;
    using IccIoForAppArguments = std::tuple<::aidl::android::hardware::radio::sim::IccIo>;
    ::ndk::ScopedAStatus iccOpenLogicalChannel(int32_t p_serial, const std::string& p_aid, int32_t p_p2) override;
    using IccOpenLogicalChannelArguments = std::tuple<std::string, int32_t>;
    ::ndk::ScopedAStatus iccTransmitApduBasicChannel(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SimApdu& p_message) override;
    using IccTransmitApduBasicChannelArguments = std::tuple<::aidl::android::hardware::radio::sim::SimApdu>;
    ::ndk::ScopedAStatus iccTransmitApduLogicalChannel(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SimApdu& p_message) override;
    using IccTransmitApduLogicalChannelArguments = std::tuple<::aidl::android::hardware::radio::sim::SimApdu>;
    ::ndk::ScopedAStatus reportStkServiceIsRunning(int32_t p_serial) override;
    using ReportStkServiceIsRunningArguments = std::tuple<>;
    ::ndk::ScopedAStatus requestIccSimAuthentication(int32_t p_serial, int32_t p_authContext, const std::string& p_authData, const std::string& p_aid) override;
    using RequestIccSimAuthenticationArguments = std::tuple<int32_t, std::string, std::string>;
    ::ndk::ScopedAStatus responseAcknowledgement() override;
    ::ndk::ScopedAStatus sendEnvelope(int32_t p_serial, const std::string& p_contents) override;
    using SendEnvelopeArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus sendEnvelopeWithStatus(int32_t p_serial, const std::string& p_contents) override;
    using SendEnvelopeWithStatusArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus sendTerminalResponseToSim(int32_t p_serial, const std::string& p_contents) override;
    using SendTerminalResponseToSimArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus setAllowedCarriers(int32_t p_serial, const ::aidl::android::hardware::radio::sim::CarrierRestrictions& p_carriers, ::aidl::android::hardware::radio::sim::SimLockMultiSimPolicy p_multiSimPolicy) override;
    using SetAllowedCarriersArguments = std::tuple<::aidl::android::hardware::radio::sim::CarrierRestrictions, ::aidl::android::hardware::radio::sim::SimLockMultiSimPolicy>;
    ::ndk::ScopedAStatus setCarrierInfoForImsiEncryption(int32_t p_serial, const ::aidl::android::hardware::radio::sim::ImsiEncryptionInfo& p_imsiEncryptionInfo) override;
    using SetCarrierInfoForImsiEncryptionArguments = std::tuple<::aidl::android::hardware::radio::sim::ImsiEncryptionInfo>;
    ::ndk::ScopedAStatus setCdmaSubscriptionSource(int32_t p_serial, ::aidl::android::hardware::radio::sim::CdmaSubscriptionSource p_cdmaSub) override;
    using SetCdmaSubscriptionSourceArguments = std::tuple<::aidl::android::hardware::radio::sim::CdmaSubscriptionSource>;
    ::ndk::ScopedAStatus setFacilityLockForApp(int32_t p_serial, const std::string& p_facility, bool p_lockState, const std::string& p_password, int32_t p_serviceClass, const std::string& p_appId) override;
    using SetFacilityLockForAppArguments = std::tuple<std::string, bool, std::string, int32_t, std::string>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::sim::IRadioSimResponse>& p_radioSimResponse, const std::shared_ptr<::aidl::android::hardware::radio::sim::IRadioSimIndication>& p_radioSimIndication) override;
    ::ndk::ScopedAStatus setSimCardPower(int32_t p_serial, ::aidl::android::hardware::radio::sim::CardPowerState p_powerUp) override;
    using SetSimCardPowerArguments = std::tuple<::aidl::android::hardware::radio::sim::CardPowerState>;
    ::ndk::ScopedAStatus setUiccSubscription(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SelectUiccSub& p_uiccSub) override;
    using SetUiccSubscriptionArguments = std::tuple<::aidl::android::hardware::radio::sim::SelectUiccSub>;
    ::ndk::ScopedAStatus supplyIccPin2ForApp(int32_t p_serial, const std::string& p_pin2, const std::string& p_aid) override;
    using SupplyIccPin2ForAppArguments = std::tuple<std::string, std::string>;
    ::ndk::ScopedAStatus supplyIccPinForApp(int32_t p_serial, const std::string& p_pin, const std::string& p_aid) override;
    using SupplyIccPinForAppArguments = std::tuple<std::string, std::string>;
    ::ndk::ScopedAStatus supplyIccPuk2ForApp(int32_t p_serial, const std::string& p_puk2, const std::string& p_pin2, const std::string& p_aid) override;
    using SupplyIccPuk2ForAppArguments = std::tuple<std::string, std::string, std::string>;
    ::ndk::ScopedAStatus supplyIccPukForApp(int32_t p_serial, const std::string& p_puk, const std::string& p_pin, const std::string& p_aid) override;
    using SupplyIccPukForAppArguments = std::tuple<std::string, std::string, std::string>;
    ::ndk::ScopedAStatus supplySimDepersonalization(int32_t p_serial, ::aidl::android::hardware::radio::sim::PersoSubstate p_persoType, const std::string& p_controlKey) override;
    using SupplySimDepersonalizationArguments = std::tuple<::aidl::android::hardware::radio::sim::PersoSubstate, std::string>;
    ::ndk::ScopedAStatus updateSimPhonebookRecords(int32_t p_serial, const ::aidl::android::hardware::radio::sim::PhonebookRecordInfo& p_recordInfo) override;
    using UpdateSimPhonebookRecordsArguments = std::tuple<::aidl::android::hardware::radio::sim::PhonebookRecordInfo>;
    ::ndk::ScopedAStatus iccCloseLogicalChannelWithSessionInfo(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SessionInfo& p_sessionInfo) override;
    using IccCloseLogicalChannelWithSessionInfoArguments = std::tuple<::aidl::android::hardware::radio::sim::SessionInfo>;
private:
    std::shared_ptr<Edge> edge_;
};
}
