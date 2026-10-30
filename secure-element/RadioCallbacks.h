// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <functional>
#include <aidl/android/hardware/radio/sim/BnRadioSimResponse.h>
#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <aidl/android/hardware/radio/sim/CardStatus.h>
#include <aidl/android/hardware/radio/sim/CarrierRestrictions.h>
#include <aidl/android/hardware/radio/sim/CdmaSubscriptionSource.h>
#include <aidl/android/hardware/radio/sim/IccIoResult.h>
#include <aidl/android/hardware/radio/sim/PersoSubstate.h>
#include <aidl/android/hardware/radio/sim/PhonebookCapacity.h>
#include <aidl/android/hardware/radio/sim/SimLockMultiSimPolicy.h>
#include <aidl/android/hardware/radio/sim/BnRadioSimIndication.h>
#include <aidl/android/hardware/radio/RadioIndicationType.h>
#include <aidl/android/hardware/radio/sim/CdmaSubscriptionSource.h>
#include <aidl/android/hardware/radio/sim/PbReceivedStatus.h>
#include <aidl/android/hardware/radio/sim/PhonebookRecordInfo.h>
#include <aidl/android/hardware/radio/sim/SimRefreshResult.h>
class RadioSimResponse : public aidl::android::hardware::radio::sim::BnRadioSimResponse {
public:
    std::function<void(const aidl::android::hardware::radio::RadioResponseInfo&, int, const std::vector<uint8_t>&, const std::string&, int, int)> reply;
    std::function<void()> changed;
    std::function<void()> acknowledge;
    ndk::ScopedAStatus acknowledgeRequest(int32_t serial) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus areUiccApplicationsEnabledResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, bool enabled) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus changeIccPin2ForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus changeIccPinForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus enableUiccApplicationsResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getAllowedCarriersResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::CarrierRestrictions& carriers, aidl::android::hardware::radio::sim::SimLockMultiSimPolicy multiSimPolicy) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCdmaSubscriptionResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const std::string& mdn, const std::string& hSid, const std::string& hNid, const std::string& min, const std::string& prl) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getCdmaSubscriptionSourceResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, aidl::android::hardware::radio::sim::CdmaSubscriptionSource source) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getFacilityLockForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t response) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getIccCardStatusResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::CardStatus& cardStatus) override { if (reply) reply(info, cardStatus.cardState, {}, cardStatus.atr, 0, 0); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getImsiForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const std::string& imsi) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getSimPhonebookCapacityResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::PhonebookCapacity& capacity) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus getSimPhonebookRecordsResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccCloseLogicalChannelResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override { if (reply) reply(info, 0, {}, {}, 0, 0); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccIoForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::IccIoResult& iccIo) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccOpenLogicalChannelResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t channelId, const std::vector<uint8_t>& selectResponse) override { if (reply) reply(info, channelId, selectResponse, {}, 0, 0); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccTransmitApduBasicChannelResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::IccIoResult& result) override { if (reply) reply(info, 0, {}, result.simResponse, result.sw1, result.sw2); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccTransmitApduLogicalChannelResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::IccIoResult& result) override { if (reply) reply(info, 0, {}, result.simResponse, result.sw1, result.sw2); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus reportStkServiceIsRunningResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus requestIccSimAuthenticationResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::IccIoResult& result) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendEnvelopeResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const std::string& commandResponse) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendEnvelopeWithStatusResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, const aidl::android::hardware::radio::sim::IccIoResult& iccIo) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus sendTerminalResponseToSimResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setAllowedCarriersResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCarrierInfoForImsiEncryptionResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setCdmaSubscriptionSourceResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setFacilityLockForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t retry) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setSimCardPowerResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override { if (reply) reply(info, 0, {}, {}, 0, 0); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setUiccSubscriptionResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPin2ForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPinForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPuk2ForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplyIccPukForAppResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus supplySimDepersonalizationResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, aidl::android::hardware::radio::sim::PersoSubstate persoType, int32_t remainingRetries) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus updateSimPhonebookRecordsResponse(const aidl::android::hardware::radio::RadioResponseInfo& info, int32_t updatedRecordIndex) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus iccCloseLogicalChannelWithSessionInfoResponse(const aidl::android::hardware::radio::RadioResponseInfo& info) override {  return ndk::ScopedAStatus::ok(); }
};
class RadioSimIndication : public aidl::android::hardware::radio::sim::BnRadioSimIndication {
public:
    std::function<void(const aidl::android::hardware::radio::RadioResponseInfo&, int, const std::vector<uint8_t>&, const std::string&, int, int)> reply;
    std::function<void()> changed;
    std::function<void()> acknowledge;
    ndk::ScopedAStatus carrierInfoForImsiEncryption(aidl::android::hardware::radio::RadioIndicationType info) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus cdmaSubscriptionSourceChanged(aidl::android::hardware::radio::RadioIndicationType type, aidl::android::hardware::radio::sim::CdmaSubscriptionSource cdmaSource) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simPhonebookChanged(aidl::android::hardware::radio::RadioIndicationType type) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simPhonebookRecordsReceived(aidl::android::hardware::radio::RadioIndicationType type, aidl::android::hardware::radio::sim::PbReceivedStatus status, const std::vector<aidl::android::hardware::radio::sim::PhonebookRecordInfo>& records) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simRefresh(aidl::android::hardware::radio::RadioIndicationType type, const aidl::android::hardware::radio::sim::SimRefreshResult& refreshResult) override { if (type == aidl::android::hardware::radio::RadioIndicationType::UNSOLICITED_ACK_EXP && acknowledge) acknowledge(); if (changed) changed(); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus simStatusChanged(aidl::android::hardware::radio::RadioIndicationType type) override { if (type == aidl::android::hardware::radio::RadioIndicationType::UNSOLICITED_ACK_EXP && acknowledge) acknowledge(); if (changed) changed(); return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stkEventNotify(aidl::android::hardware::radio::RadioIndicationType type, const std::string& cmd) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stkProactiveCommand(aidl::android::hardware::radio::RadioIndicationType type, const std::string& cmd) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus stkSessionEnd(aidl::android::hardware::radio::RadioIndicationType type) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus subscriptionStatusChanged(aidl::android::hardware::radio::RadioIndicationType type, bool activate) override {  return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus uiccApplicationsEnablementChanged(aidl::android::hardware::radio::RadioIndicationType type, bool enabled) override {  return ndk::ScopedAStatus::ok(); }
};
