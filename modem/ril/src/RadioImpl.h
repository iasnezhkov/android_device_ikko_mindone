/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mind_one minimal RIL, phase 1 skeleton (RIL-MINIMAL-1409).
//
// MindoneRadio implements android.hardware.radio@1.6::IRadio for instance "slot1". Version
// choice is not arbitrary -- see RIL-MINIMAL-1409 "HAL version choice": the device's
// framework compatibility matrix (compatibility_matrix.6.xml, target-level 6, matching this
// device's manifest.xml <manifest target-level="6">) requires HIDL android.hardware.radio
// version "1.5-6" for instance slot1/slot2/slot3, and the stock librilfusion.so itself links
// android.hardware.radio@1.0.so through @1.6.so (readelf -d, confirmed in the LineageOS tree) -- i.e.
// the real vendor RIL already implements 1.6 for this exact instance. Implementing anything
// lower would fail VINTF compatibility.
//
// Every one of IRadio@1.6's 201 pure-virtual request methods must be overridden (HIDL C++ has
// no default-stub mechanism); this file's method table below was generated mechanically from
// the real android/hardware/radio/1.6/IRadio.h (parsed by
// the (untracked) codegen helper parse_iradio.py) so every signature is copy-exact -- no
// hand-transcription errors. ~24 of them (the task's minimal request set: SIM status, signal
// strength, voice/data registration, operator, dial/hangup/currentCalls/acceptCall, sendSms,
// setupDataCall/deactivateDataCall in all their 1.0/1.2/1.4/1.5/1.6 forms, setRadioPower,
// getDeviceIdentity, plus the two non-request plumbing methods setResponseFunctions/
// responseAcknowledgement) are implemented faithfully in RadioImpl_core.cpp against a real
// AtChannel. The remaining ~177 are auto-generated in RadioImpl_stubs.cpp: each answers
// RadioError::REQUEST_NOT_SUPPORTED through the *exact* IRadioResponse callback method the
// framework expects for that request (resolved by the (untracked) codegen helper parse_iradioresponse.py
// from the real IRadioResponse@1.6.h, not guessed) -- "no silent stubs" per the task brief: every
// unsupported request still gets a spec-correct response, just an honest one.
#pragma once

#include <android/hardware/radio/1.6/IRadio.h>
#include <android/hardware/radio/1.6/IRadioResponse.h>
#include <android/hardware/radio/1.6/IRadioIndication.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/Status.h>

#include <memory>
#include <mutex>
#include <string>

#include "AtChannel.h"

namespace mindone::ril {

// Implements android.hardware.radio@1.6::IRadio/slot1. See file header above and
// RIL-MINIMAL-1409 for the version/scope rationale.
class MindoneRadio : public ::android::hardware::radio::V1_6::IRadio {
  public:
    MindoneRadio();
    ~MindoneRadio() override;

    // Opens the two AT channels (command + notification, see AtChannel.h) against the given
    // device paths. Must be called before registerAsService(); returns false if either open()
    // fails (e.g. gsm0710muxd hasn't created the pty yet -- caller should retry, see
    // service.cpp). Does NOT itself fire the boot handshake (see RadioImpl_core.cpp
    // "connect-time probe" comment) -- that only happens once the framework actually binds via
    // setResponseFunctions(), matching the stock RIL's own lifecycle (a HIDL server with no
    // bound response callback has nowhere to deliver responses/indications to).
    bool openAtChannels(const std::string& cmdDevicePath, const std::string& notiDevicePath);

    ::android::hardware::Return<void> setResponseFunctions(const ::android::sp<::android::hardware::radio::V1_0::IRadioResponse>& radioResponse, const ::android::sp<::android::hardware::radio::V1_0::IRadioIndication>& radioIndication) override;
    ::android::hardware::Return<void> getIccCardStatus(int32_t serial) override;
    ::android::hardware::Return<void> supplyIccPinForApp(int32_t serial, const ::android::hardware::hidl_string& pin, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> supplyIccPukForApp(int32_t serial, const ::android::hardware::hidl_string& puk, const ::android::hardware::hidl_string& pin, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> supplyIccPin2ForApp(int32_t serial, const ::android::hardware::hidl_string& pin2, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> supplyIccPuk2ForApp(int32_t serial, const ::android::hardware::hidl_string& puk2, const ::android::hardware::hidl_string& pin2, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> changeIccPinForApp(int32_t serial, const ::android::hardware::hidl_string& oldPin, const ::android::hardware::hidl_string& newPin, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> changeIccPin2ForApp(int32_t serial, const ::android::hardware::hidl_string& oldPin2, const ::android::hardware::hidl_string& newPin2, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> supplyNetworkDepersonalization(int32_t serial, const ::android::hardware::hidl_string& netPin) override;
    ::android::hardware::Return<void> getCurrentCalls(int32_t serial) override;
    ::android::hardware::Return<void> dial(int32_t serial, const ::android::hardware::radio::V1_0::Dial& dialInfo) override;
    ::android::hardware::Return<void> getImsiForApp(int32_t serial, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> hangup(int32_t serial, int32_t gsmIndex) override;
    ::android::hardware::Return<void> hangupWaitingOrBackground(int32_t serial) override;
    ::android::hardware::Return<void> hangupForegroundResumeBackground(int32_t serial) override;
    ::android::hardware::Return<void> switchWaitingOrHoldingAndActive(int32_t serial) override;
    ::android::hardware::Return<void> conference(int32_t serial) override;
    ::android::hardware::Return<void> rejectCall(int32_t serial) override;
    ::android::hardware::Return<void> getLastCallFailCause(int32_t serial) override;
    ::android::hardware::Return<void> getSignalStrength(int32_t serial) override;
    ::android::hardware::Return<void> getVoiceRegistrationState(int32_t serial) override;
    ::android::hardware::Return<void> getDataRegistrationState(int32_t serial) override;
    ::android::hardware::Return<void> getOperator(int32_t serial) override;
    ::android::hardware::Return<void> setRadioPower(int32_t serial, bool on) override;
    ::android::hardware::Return<void> sendDtmf(int32_t serial, const ::android::hardware::hidl_string& s) override;
    ::android::hardware::Return<void> sendSms(int32_t serial, const ::android::hardware::radio::V1_0::GsmSmsMessage& message) override;
    ::android::hardware::Return<void> sendSMSExpectMore(int32_t serial, const ::android::hardware::radio::V1_0::GsmSmsMessage& message) override;
    ::android::hardware::Return<void> setupDataCall(int32_t serial, ::android::hardware::radio::V1_0::RadioTechnology radioTechnology, const ::android::hardware::radio::V1_0::DataProfileInfo& dataProfileInfo, bool modemCognitive, bool roamingAllowed, bool isRoaming) override;
    ::android::hardware::Return<void> iccIOForApp(int32_t serial, const ::android::hardware::radio::V1_0::IccIo& iccIo) override;
    ::android::hardware::Return<void> sendUssd(int32_t serial, const ::android::hardware::hidl_string& ussd) override;
    ::android::hardware::Return<void> cancelPendingUssd(int32_t serial) override;
    ::android::hardware::Return<void> getClir(int32_t serial) override;
    ::android::hardware::Return<void> setClir(int32_t serial, int32_t status) override;
    ::android::hardware::Return<void> getCallForwardStatus(int32_t serial, const ::android::hardware::radio::V1_0::CallForwardInfo& callInfo) override;
    ::android::hardware::Return<void> setCallForward(int32_t serial, const ::android::hardware::radio::V1_0::CallForwardInfo& callInfo) override;
    ::android::hardware::Return<void> getCallWaiting(int32_t serial, int32_t serviceClass) override;
    ::android::hardware::Return<void> setCallWaiting(int32_t serial, bool enable, int32_t serviceClass) override;
    ::android::hardware::Return<void> acknowledgeLastIncomingGsmSms(int32_t serial, bool success, ::android::hardware::radio::V1_0::SmsAcknowledgeFailCause cause) override;
    ::android::hardware::Return<void> acceptCall(int32_t serial) override;
    ::android::hardware::Return<void> deactivateDataCall(int32_t serial, int32_t cid, bool reasonRadioShutDown) override;
    ::android::hardware::Return<void> getFacilityLockForApp(int32_t serial, const ::android::hardware::hidl_string& facility, const ::android::hardware::hidl_string& password, int32_t serviceClass, const ::android::hardware::hidl_string& appId) override;
    ::android::hardware::Return<void> setFacilityLockForApp(int32_t serial, const ::android::hardware::hidl_string& facility, bool lockState, const ::android::hardware::hidl_string& password, int32_t serviceClass, const ::android::hardware::hidl_string& appId) override;
    ::android::hardware::Return<void> setBarringPassword(int32_t serial, const ::android::hardware::hidl_string& facility, const ::android::hardware::hidl_string& oldPassword, const ::android::hardware::hidl_string& newPassword) override;
    ::android::hardware::Return<void> getNetworkSelectionMode(int32_t serial) override;
    ::android::hardware::Return<void> setNetworkSelectionModeAutomatic(int32_t serial) override;
    ::android::hardware::Return<void> setNetworkSelectionModeManual(int32_t serial, const ::android::hardware::hidl_string& operatorNumeric) override;
    ::android::hardware::Return<void> getAvailableNetworks(int32_t serial) override;
    ::android::hardware::Return<void> startDtmf(int32_t serial, const ::android::hardware::hidl_string& s) override;
    ::android::hardware::Return<void> stopDtmf(int32_t serial) override;
    ::android::hardware::Return<void> getBasebandVersion(int32_t serial) override;
    ::android::hardware::Return<void> separateConnection(int32_t serial, int32_t gsmIndex) override;
    ::android::hardware::Return<void> setMute(int32_t serial, bool enable) override;
    ::android::hardware::Return<void> getMute(int32_t serial) override;
    ::android::hardware::Return<void> getClip(int32_t serial) override;
    ::android::hardware::Return<void> getDataCallList(int32_t serial) override;
    ::android::hardware::Return<void> setSuppServiceNotifications(int32_t serial, bool enable) override;
    ::android::hardware::Return<void> writeSmsToSim(int32_t serial, const ::android::hardware::radio::V1_0::SmsWriteArgs& smsWriteArgs) override;
    ::android::hardware::Return<void> deleteSmsOnSim(int32_t serial, int32_t index) override;
    ::android::hardware::Return<void> setBandMode(int32_t serial, ::android::hardware::radio::V1_0::RadioBandMode mode) override;
    ::android::hardware::Return<void> getAvailableBandModes(int32_t serial) override;
    ::android::hardware::Return<void> sendEnvelope(int32_t serial, const ::android::hardware::hidl_string& command) override;
    ::android::hardware::Return<void> sendTerminalResponseToSim(int32_t serial, const ::android::hardware::hidl_string& commandResponse) override;
    ::android::hardware::Return<void> handleStkCallSetupRequestFromSim(int32_t serial, bool accept) override;
    ::android::hardware::Return<void> explicitCallTransfer(int32_t serial) override;
    ::android::hardware::Return<void> setPreferredNetworkType(int32_t serial, ::android::hardware::radio::V1_0::PreferredNetworkType nwType) override;
    ::android::hardware::Return<void> getPreferredNetworkType(int32_t serial) override;
    ::android::hardware::Return<void> getNeighboringCids(int32_t serial) override;
    ::android::hardware::Return<void> setLocationUpdates(int32_t serial, bool enable) override;
    ::android::hardware::Return<void> setCdmaSubscriptionSource(int32_t serial, ::android::hardware::radio::V1_0::CdmaSubscriptionSource cdmaSub) override;
    ::android::hardware::Return<void> setCdmaRoamingPreference(int32_t serial, ::android::hardware::radio::V1_0::CdmaRoamingType type) override;
    ::android::hardware::Return<void> getCdmaRoamingPreference(int32_t serial) override;
    ::android::hardware::Return<void> setTTYMode(int32_t serial, ::android::hardware::radio::V1_0::TtyMode mode) override;
    ::android::hardware::Return<void> getTTYMode(int32_t serial) override;
    ::android::hardware::Return<void> setPreferredVoicePrivacy(int32_t serial, bool enable) override;
    ::android::hardware::Return<void> getPreferredVoicePrivacy(int32_t serial) override;
    ::android::hardware::Return<void> sendCDMAFeatureCode(int32_t serial, const ::android::hardware::hidl_string& featureCode) override;
    ::android::hardware::Return<void> sendBurstDtmf(int32_t serial, const ::android::hardware::hidl_string& dtmf, int32_t on, int32_t off) override;
    ::android::hardware::Return<void> sendCdmaSms(int32_t serial, const ::android::hardware::radio::V1_0::CdmaSmsMessage& sms) override;
    ::android::hardware::Return<void> acknowledgeLastIncomingCdmaSms(int32_t serial, const ::android::hardware::radio::V1_0::CdmaSmsAck& smsAck) override;
    ::android::hardware::Return<void> getGsmBroadcastConfig(int32_t serial) override;
    ::android::hardware::Return<void> setGsmBroadcastConfig(int32_t serial, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_0::GsmBroadcastSmsConfigInfo>& configInfo) override;
    ::android::hardware::Return<void> setGsmBroadcastActivation(int32_t serial, bool activate) override;
    ::android::hardware::Return<void> getCdmaBroadcastConfig(int32_t serial) override;
    ::android::hardware::Return<void> setCdmaBroadcastConfig(int32_t serial, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_0::CdmaBroadcastSmsConfigInfo>& configInfo) override;
    ::android::hardware::Return<void> setCdmaBroadcastActivation(int32_t serial, bool activate) override;
    ::android::hardware::Return<void> getCDMASubscription(int32_t serial) override;
    ::android::hardware::Return<void> writeSmsToRuim(int32_t serial, const ::android::hardware::radio::V1_0::CdmaSmsWriteArgs& cdmaSms) override;
    ::android::hardware::Return<void> deleteSmsOnRuim(int32_t serial, int32_t index) override;
    ::android::hardware::Return<void> getDeviceIdentity(int32_t serial) override;
    ::android::hardware::Return<void> exitEmergencyCallbackMode(int32_t serial) override;
    ::android::hardware::Return<void> getSmscAddress(int32_t serial) override;
    ::android::hardware::Return<void> setSmscAddress(int32_t serial, const ::android::hardware::hidl_string& smsc) override;
    ::android::hardware::Return<void> reportSmsMemoryStatus(int32_t serial, bool available) override;
    ::android::hardware::Return<void> reportStkServiceIsRunning(int32_t serial) override;
    ::android::hardware::Return<void> getCdmaSubscriptionSource(int32_t serial) override;
    ::android::hardware::Return<void> requestIsimAuthentication(int32_t serial, const ::android::hardware::hidl_string& challenge) override;
    ::android::hardware::Return<void> acknowledgeIncomingGsmSmsWithPdu(int32_t serial, bool success, const ::android::hardware::hidl_string& ackPdu) override;
    ::android::hardware::Return<void> sendEnvelopeWithStatus(int32_t serial, const ::android::hardware::hidl_string& contents) override;
    ::android::hardware::Return<void> getVoiceRadioTechnology(int32_t serial) override;
    ::android::hardware::Return<void> getCellInfoList(int32_t serial) override;
    ::android::hardware::Return<void> setCellInfoListRate(int32_t serial, int32_t rate) override;
    ::android::hardware::Return<void> setInitialAttachApn(int32_t serial, const ::android::hardware::radio::V1_0::DataProfileInfo& dataProfileInfo, bool modemCognitive, bool isRoaming) override;
    ::android::hardware::Return<void> getImsRegistrationState(int32_t serial) override;
    ::android::hardware::Return<void> sendImsSms(int32_t serial, const ::android::hardware::radio::V1_0::ImsSmsMessage& message) override;
    ::android::hardware::Return<void> iccTransmitApduBasicChannel(int32_t serial, const ::android::hardware::radio::V1_0::SimApdu& message) override;
    ::android::hardware::Return<void> iccOpenLogicalChannel(int32_t serial, const ::android::hardware::hidl_string& aid, int32_t p2) override;
    ::android::hardware::Return<void> iccCloseLogicalChannel(int32_t serial, int32_t channelId) override;
    ::android::hardware::Return<void> iccTransmitApduLogicalChannel(int32_t serial, const ::android::hardware::radio::V1_0::SimApdu& message) override;
    ::android::hardware::Return<void> nvReadItem(int32_t serial, ::android::hardware::radio::V1_0::NvItem itemId) override;
    ::android::hardware::Return<void> nvWriteItem(int32_t serial, const ::android::hardware::radio::V1_0::NvWriteItem& item) override;
    ::android::hardware::Return<void> nvWriteCdmaPrl(int32_t serial, const ::android::hardware::hidl_vec<uint8_t>& prl) override;
    ::android::hardware::Return<void> nvResetConfig(int32_t serial, ::android::hardware::radio::V1_0::ResetNvType resetType) override;
    ::android::hardware::Return<void> setUiccSubscription(int32_t serial, const ::android::hardware::radio::V1_0::SelectUiccSub& uiccSub) override;
    ::android::hardware::Return<void> setDataAllowed(int32_t serial, bool allow) override;
    ::android::hardware::Return<void> getHardwareConfig(int32_t serial) override;
    ::android::hardware::Return<void> requestIccSimAuthentication(int32_t serial, int32_t authContext, const ::android::hardware::hidl_string& authData, const ::android::hardware::hidl_string& aid) override;
    ::android::hardware::Return<void> setDataProfile(int32_t serial, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_0::DataProfileInfo>& profiles, bool isRoaming) override;
    ::android::hardware::Return<void> requestShutdown(int32_t serial) override;
    ::android::hardware::Return<void> getRadioCapability(int32_t serial) override;
    ::android::hardware::Return<void> setRadioCapability(int32_t serial, const ::android::hardware::radio::V1_0::RadioCapability& rc) override;
    ::android::hardware::Return<void> startLceService(int32_t serial, int32_t reportInterval, bool pullMode) override;
    ::android::hardware::Return<void> stopLceService(int32_t serial) override;
    ::android::hardware::Return<void> pullLceData(int32_t serial) override;
    ::android::hardware::Return<void> getModemActivityInfo(int32_t serial) override;
    ::android::hardware::Return<void> setAllowedCarriers(int32_t serial, bool allAllowed, const ::android::hardware::radio::V1_0::CarrierRestrictions& carriers) override;
    ::android::hardware::Return<void> getAllowedCarriers(int32_t serial) override;
    ::android::hardware::Return<void> sendDeviceState(int32_t serial, ::android::hardware::radio::V1_0::DeviceStateType deviceStateType, bool state) override;
    ::android::hardware::Return<void> setIndicationFilter(int32_t serial, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_0::IndicationFilter> indicationFilter) override;
    ::android::hardware::Return<void> setSimCardPower(int32_t serial, bool powerUp) override;
    ::android::hardware::Return<void> responseAcknowledgement() override;
    ::android::hardware::Return<void> setCarrierInfoForImsiEncryption(int32_t serial, const ::android::hardware::radio::V1_1::ImsiEncryptionInfo& imsiEncryptionInfo) override;
    ::android::hardware::Return<void> setSimCardPower_1_1(int32_t serial, ::android::hardware::radio::V1_1::CardPowerState powerUp) override;
    ::android::hardware::Return<void> startNetworkScan(int32_t serial, const ::android::hardware::radio::V1_1::NetworkScanRequest& request) override;
    ::android::hardware::Return<void> stopNetworkScan(int32_t serial) override;
    ::android::hardware::Return<void> startKeepalive(int32_t serial, const ::android::hardware::radio::V1_1::KeepaliveRequest& keepalive) override;
    ::android::hardware::Return<void> stopKeepalive(int32_t serial, int32_t sessionHandle) override;
    ::android::hardware::Return<void> startNetworkScan_1_2(int32_t serial, const ::android::hardware::radio::V1_2::NetworkScanRequest& request) override;
    ::android::hardware::Return<void> setIndicationFilter_1_2(int32_t serial, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_2::IndicationFilter> indicationFilter) override;
    ::android::hardware::Return<void> setSignalStrengthReportingCriteria(int32_t serial, int32_t hysteresisMs, int32_t hysteresisDb, const ::android::hardware::hidl_vec<int32_t>& thresholdsDbm, ::android::hardware::radio::V1_2::AccessNetwork accessNetwork) override;
    ::android::hardware::Return<void> setLinkCapacityReportingCriteria(int32_t serial, int32_t hysteresisMs, int32_t hysteresisDlKbps, int32_t hysteresisUlKbps, const ::android::hardware::hidl_vec<int32_t>& thresholdsDownlinkKbps, const ::android::hardware::hidl_vec<int32_t>& thresholdsUplinkKbps, ::android::hardware::radio::V1_2::AccessNetwork accessNetwork) override;
    ::android::hardware::Return<void> setupDataCall_1_2(int32_t serial, ::android::hardware::radio::V1_2::AccessNetwork accessNetwork, const ::android::hardware::radio::V1_0::DataProfileInfo& dataProfileInfo, bool modemCognitive, bool roamingAllowed, bool isRoaming, ::android::hardware::radio::V1_2::DataRequestReason reason, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& addresses, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& dnses) override;
    ::android::hardware::Return<void> deactivateDataCall_1_2(int32_t serial, int32_t cid, ::android::hardware::radio::V1_2::DataRequestReason reason) override;
    ::android::hardware::Return<void> setSystemSelectionChannels(int32_t serial, bool specifyChannels, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_1::RadioAccessSpecifier>& specifiers) override;
    ::android::hardware::Return<void> enableModem(int32_t serial, bool on) override;
    ::android::hardware::Return<void> getModemStackStatus(int32_t serial) override;
    ::android::hardware::Return<void> setupDataCall_1_4(int32_t serial, ::android::hardware::radio::V1_4::AccessNetwork accessNetwork, const ::android::hardware::radio::V1_4::DataProfileInfo& dataProfileInfo, bool roamingAllowed, ::android::hardware::radio::V1_2::DataRequestReason reason, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& addresses, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& dnses) override;
    ::android::hardware::Return<void> setInitialAttachApn_1_4(int32_t serial, const ::android::hardware::radio::V1_4::DataProfileInfo& dataProfileInfo) override;
    ::android::hardware::Return<void> setDataProfile_1_4(int32_t serial, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_4::DataProfileInfo>& profiles) override;
    ::android::hardware::Return<void> emergencyDial(int32_t serial, const ::android::hardware::radio::V1_0::Dial& dialInfo, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_4::EmergencyServiceCategory> categories, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& urns, ::android::hardware::radio::V1_4::EmergencyCallRouting routing, bool hasKnownUserIntentEmergency, bool isTesting) override;
    ::android::hardware::Return<void> startNetworkScan_1_4(int32_t serial, const ::android::hardware::radio::V1_2::NetworkScanRequest& request) override;
    ::android::hardware::Return<void> getPreferredNetworkTypeBitmap(int32_t serial) override;
    ::android::hardware::Return<void> setPreferredNetworkTypeBitmap(int32_t serial, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_4::RadioAccessFamily> networkTypeBitmap) override;
    ::android::hardware::Return<void> setAllowedCarriers_1_4(int32_t serial, const ::android::hardware::radio::V1_4::CarrierRestrictionsWithPriority& carriers, ::android::hardware::radio::V1_4::SimLockMultiSimPolicy multiSimPolicy) override;
    ::android::hardware::Return<void> getAllowedCarriers_1_4(int32_t serial) override;
    ::android::hardware::Return<void> getSignalStrength_1_4(int32_t serial) override;
    ::android::hardware::Return<void> setSignalStrengthReportingCriteria_1_5(int32_t serial, const ::android::hardware::radio::V1_5::SignalThresholdInfo& signalThresholdInfo, ::android::hardware::radio::V1_5::AccessNetwork accessNetwork) override;
    ::android::hardware::Return<void> setLinkCapacityReportingCriteria_1_5(int32_t serial, int32_t hysteresisMs, int32_t hysteresisDlKbps, int32_t hysteresisUlKbps, const ::android::hardware::hidl_vec<int32_t>& thresholdsDownlinkKbps, const ::android::hardware::hidl_vec<int32_t>& thresholdsUplinkKbps, ::android::hardware::radio::V1_5::AccessNetwork accessNetwork) override;
    ::android::hardware::Return<void> enableUiccApplications(int32_t serial, bool enable) override;
    ::android::hardware::Return<void> areUiccApplicationsEnabled(int32_t serial) override;
    ::android::hardware::Return<void> setSystemSelectionChannels_1_5(int32_t serial, bool specifyChannels, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_5::RadioAccessSpecifier>& specifiers) override;
    ::android::hardware::Return<void> startNetworkScan_1_5(int32_t serial, const ::android::hardware::radio::V1_5::NetworkScanRequest& request) override;
    ::android::hardware::Return<void> setupDataCall_1_5(int32_t serial, ::android::hardware::radio::V1_5::AccessNetwork accessNetwork, const ::android::hardware::radio::V1_5::DataProfileInfo& dataProfileInfo, bool roamingAllowed, ::android::hardware::radio::V1_2::DataRequestReason reason, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_5::LinkAddress>& addresses, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& dnses) override;
    ::android::hardware::Return<void> setInitialAttachApn_1_5(int32_t serial, const ::android::hardware::radio::V1_5::DataProfileInfo& dataProfileInfo) override;
    ::android::hardware::Return<void> setDataProfile_1_5(int32_t serial, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_5::DataProfileInfo>& profiles) override;
    ::android::hardware::Return<void> setRadioPower_1_5(int32_t serial, bool powerOn, bool forEmergencyCall, bool preferredForEmergencyCall) override;
    ::android::hardware::Return<void> setIndicationFilter_1_5(int32_t serial, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_5::IndicationFilter> indicationFilter) override;
    ::android::hardware::Return<void> getBarringInfo(int32_t serial) override;
    ::android::hardware::Return<void> getVoiceRegistrationState_1_5(int32_t serial) override;
    ::android::hardware::Return<void> getDataRegistrationState_1_5(int32_t serial) override;
    ::android::hardware::Return<void> setNetworkSelectionModeManual_1_5(int32_t serial, const ::android::hardware::hidl_string& operatorNumeric, ::android::hardware::radio::V1_5::RadioAccessNetworks ran) override;
    ::android::hardware::Return<void> sendCdmaSmsExpectMore(int32_t serial, const ::android::hardware::radio::V1_0::CdmaSmsMessage& sms) override;
    ::android::hardware::Return<void> supplySimDepersonalization(int32_t serial, ::android::hardware::radio::V1_5::PersoSubstate persoType, const ::android::hardware::hidl_string& controlKey) override;
    ::android::hardware::Return<void> setRadioPower_1_6(int32_t serial, bool powerOn, bool forEmergencyCall, bool preferredForEmergencyCall) override;
    ::android::hardware::Return<void> getDataCallList_1_6(int32_t serial) override;
    ::android::hardware::Return<void> setupDataCall_1_6(int32_t serial, ::android::hardware::radio::V1_5::AccessNetwork accessNetwork, const ::android::hardware::radio::V1_5::DataProfileInfo& dataProfileInfo, bool roamingAllowed, ::android::hardware::radio::V1_2::DataRequestReason reason, const ::android::hardware::hidl_vec<::android::hardware::radio::V1_5::LinkAddress>& addresses, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& dnses, int32_t pduSessionId, const ::android::hardware::radio::V1_6::OptionalSliceInfo& sliceInfo, const ::android::hardware::radio::V1_6::OptionalTrafficDescriptor& trafficDescriptor, bool matchAllRuleAllowed) override;
    ::android::hardware::Return<void> sendSms_1_6(int32_t serial, const ::android::hardware::radio::V1_0::GsmSmsMessage& message) override;
    ::android::hardware::Return<void> sendSmsExpectMore_1_6(int32_t serial, const ::android::hardware::radio::V1_0::GsmSmsMessage& message) override;
    ::android::hardware::Return<void> sendCdmaSms_1_6(int32_t serial, const ::android::hardware::radio::V1_0::CdmaSmsMessage& sms) override;
    ::android::hardware::Return<void> sendCdmaSmsExpectMore_1_6(int32_t serial, const ::android::hardware::radio::V1_0::CdmaSmsMessage& sms) override;
    ::android::hardware::Return<void> setSimCardPower_1_6(int32_t serial, ::android::hardware::radio::V1_1::CardPowerState powerUp) override;
    ::android::hardware::Return<void> setNrDualConnectivityState(int32_t serial, ::android::hardware::radio::V1_6::NrDualConnectivityState nrDualConnectivityState) override;
    ::android::hardware::Return<void> isNrDualConnectivityEnabled(int32_t serial) override;
    ::android::hardware::Return<void> allocatePduSessionId(int32_t serial) override;
    ::android::hardware::Return<void> releasePduSessionId(int32_t serial, int32_t id) override;
    ::android::hardware::Return<void> startHandover(int32_t serial, int32_t callId) override;
    ::android::hardware::Return<void> cancelHandover(int32_t serial, int32_t callId) override;
    ::android::hardware::Return<void> setAllowedNetworkTypesBitmap(uint32_t serial, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_4::RadioAccessFamily> networkTypeBitmap) override;
    ::android::hardware::Return<void> getAllowedNetworkTypesBitmap(int32_t serial) override;
    ::android::hardware::Return<void> setDataThrottling(int32_t serial, ::android::hardware::radio::V1_6::DataThrottlingAction dataThrottlingAction, int64_t completionDurationMillis) override;
    ::android::hardware::Return<void> emergencyDial_1_6(int32_t serial, const ::android::hardware::radio::V1_0::Dial& dialInfo, ::android::hardware::hidl_bitfield<::android::hardware::radio::V1_4::EmergencyServiceCategory> categories, const ::android::hardware::hidl_vec<::android::hardware::hidl_string>& urns, ::android::hardware::radio::V1_4::EmergencyCallRouting routing, bool hasKnownUserIntentEmergency, bool isTesting) override;
    ::android::hardware::Return<void> getSystemSelectionChannels(int32_t serial) override;
    ::android::hardware::Return<void> getCellInfoList_1_6(int32_t serial) override;
    ::android::hardware::Return<void> getVoiceRegistrationState_1_6(int32_t serial) override;
    ::android::hardware::Return<void> getSignalStrength_1_6(int32_t serial) override;
    ::android::hardware::Return<void> getDataRegistrationState_1_6(int32_t serial) override;
    ::android::hardware::Return<void> getCurrentCalls_1_6(int32_t serial) override;
    ::android::hardware::Return<void> getSlicingConfig(int32_t serial) override;
    ::android::hardware::Return<void> setCarrierInfoForImsiEncryption_1_6(int32_t serial, const ::android::hardware::radio::V1_6::ImsiEncryptionInfo& imsiEncryptionInfo) override;
    ::android::hardware::Return<void> getSimPhonebookRecords(int32_t serial) override;
    ::android::hardware::Return<void> getSimPhonebookCapacity(int32_t serial) override;
    ::android::hardware::Return<void> updateSimPhonebookRecords(int32_t serial, const ::android::hardware::radio::V1_6::PhonebookRecordInfo& recordInfo) override;
  private:
    // --- helpers shared by RadioImpl_core.cpp and RadioImpl_stubs.cpp ---
    static ::android::hardware::radio::V1_0::RadioResponseInfo mkInfo(
        int32_t serial, ::android::hardware::radio::V1_0::RadioError error);
    static ::android::hardware::radio::V1_6::RadioResponseInfo mkInfo16(
        int32_t serial, ::android::hardware::radio::V1_6::RadioError error);
    static void logStubRequest(const char* name, int32_t serial);

    // Boot handshake (RIL-MINIMAL-1409 "boot handshake", recovered from
    // RmcRadioRequestHandler's real constructor, modem/ril/AT-MAP-NOTES.md
    // "Boot-handshake findings" -- NOT the task's originally-guessed command names, three of
    // which do not exist in the binary at all). Runs once, synchronously, right after
    // setResponseFunctions() binds a caller for the first time. Every command here is
    // read-or-runtime-config, never a factory/NVRAM write -- see the blacklist in
    // RIL-MINIMAL-1409 "risks" for the write-shaped AT+E* commands this skeleton must
    // never send.
    void runBootHandshake();

    // Shared implementation for the five setupDataCall generations (1.0/1.2/1.4/1.5/1.6) and
    // the two deactivateDataCall generations (1.0/1.2) -- see RadioImpl_core.cpp. Only a
    // single-APN, single-PDN default-bearer case is implemented; anything else (multiple
    // simultaneous PDNs, non-IP protocol, non-default DataRequestReason) answers
    // RadioError::REQUEST_NOT_SUPPORTED, logged loudly rather than guessed at.
    ::android::hardware::radio::V1_6::SetupDataCallResult doSetupDataCall(
        int32_t cid, const std::string& apn, const std::string& protocol);
    bool doDeactivateDataCall(int32_t cid);

    // Shared implementation for getVoiceRegistrationState[_1_6]/getDataRegistrationState[_1_6]
    // -- see RadioImpl_core.cpp.
    struct RegState {
        int32_t regState = 0;   // V1_0::RegState numeric value
        int32_t rat = 0;        // V1_0::RadioTechnology numeric value, best-effort from +CREG/+CGREG
    };
    RegState queryRegState(bool isData);

    AtChannel mCmdChannel;
    AtChannel mNotiChannel;
    std::mutex mAtLock;  // serializes AtChannel::sendCommand across concurrent IRadio callers
                          // (the framework may call from more than one binder thread; a real AT
                          // tty is one physical command queue, matching AtChannel's own
                          // single-command-in-flight design, see AtChannel.h)

    ::android::sp<::android::hardware::radio::V1_0::IRadioResponse> mResponseV1_0;
    ::android::sp<::android::hardware::radio::V1_6::IRadioResponse> mResponseV1_6;
    ::android::sp<::android::hardware::radio::V1_0::IRadioIndication> mIndicationV1_0;
    ::android::sp<::android::hardware::radio::V1_6::IRadioIndication> mIndicationV1_6;

    bool mBootHandshakeDone = false;
    bool mRadioPowerOn = false;

    // Applies the modem's own idle power-saving levers once the radio is on (F4486).
    // Caller must already hold mAtLock.
    void applyPowerSavingProfileLocked();
    bool mPowerSavingApplied = false;
};

}  // namespace mindone::ril
