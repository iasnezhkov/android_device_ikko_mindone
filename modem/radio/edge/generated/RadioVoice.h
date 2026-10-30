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
#include <aidl/android/hardware/radio/voice/BnRadioVoice.h>
#include <aidl/android/hardware/radio/voice/CallForwardInfo.h>
#include <aidl/android/hardware/radio/voice/Dial.h>
#include <aidl/android/hardware/radio/voice/EmergencyCallRouting.h>
#include <aidl/android/hardware/radio/voice/IRadioVoiceIndication.h>
#include <aidl/android/hardware/radio/voice/IRadioVoiceResponse.h>
#include <aidl/android/hardware/radio/voice/TtyMode.h>
namespace mindone::radio {
class RadioVoice final : public ::aidl::android::hardware::radio::voice::BnRadioVoice {
public:
    explicit RadioVoice(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus acceptCall(int32_t p_serial) override;
    using AcceptCallArguments = std::tuple<>;
    ::ndk::ScopedAStatus cancelPendingUssd(int32_t p_serial) override;
    using CancelPendingUssdArguments = std::tuple<>;
    ::ndk::ScopedAStatus conference(int32_t p_serial) override;
    using ConferenceArguments = std::tuple<>;
    ::ndk::ScopedAStatus dial(int32_t p_serial, const ::aidl::android::hardware::radio::voice::Dial& p_dialInfo) override;
    using DialArguments = std::tuple<::aidl::android::hardware::radio::voice::Dial>;
    ::ndk::ScopedAStatus emergencyDial(int32_t p_serial, const ::aidl::android::hardware::radio::voice::Dial& p_dialInfo, int32_t p_categories, const std::vector<std::string>& p_urns, ::aidl::android::hardware::radio::voice::EmergencyCallRouting p_routing, bool p_hasKnownUserIntentEmergency, bool p_isTesting) override;
    using EmergencyDialArguments = std::tuple<::aidl::android::hardware::radio::voice::Dial, int32_t, std::vector<std::string>, ::aidl::android::hardware::radio::voice::EmergencyCallRouting, bool, bool>;
    ::ndk::ScopedAStatus exitEmergencyCallbackMode(int32_t p_serial) override;
    using ExitEmergencyCallbackModeArguments = std::tuple<>;
    ::ndk::ScopedAStatus explicitCallTransfer(int32_t p_serial) override;
    using ExplicitCallTransferArguments = std::tuple<>;
    ::ndk::ScopedAStatus getCallForwardStatus(int32_t p_serial, const ::aidl::android::hardware::radio::voice::CallForwardInfo& p_callInfo) override;
    using GetCallForwardStatusArguments = std::tuple<::aidl::android::hardware::radio::voice::CallForwardInfo>;
    ::ndk::ScopedAStatus getCallWaiting(int32_t p_serial, int32_t p_serviceClass) override;
    using GetCallWaitingArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus getClip(int32_t p_serial) override;
    using GetClipArguments = std::tuple<>;
    ::ndk::ScopedAStatus getClir(int32_t p_serial) override;
    using GetClirArguments = std::tuple<>;
    ::ndk::ScopedAStatus getCurrentCalls(int32_t p_serial) override;
    using GetCurrentCallsArguments = std::tuple<>;
    ::ndk::ScopedAStatus getLastCallFailCause(int32_t p_serial) override;
    using GetLastCallFailCauseArguments = std::tuple<>;
    ::ndk::ScopedAStatus getMute(int32_t p_serial) override;
    using GetMuteArguments = std::tuple<>;
    ::ndk::ScopedAStatus getPreferredVoicePrivacy(int32_t p_serial) override;
    using GetPreferredVoicePrivacyArguments = std::tuple<>;
    ::ndk::ScopedAStatus getTtyMode(int32_t p_serial) override;
    using GetTtyModeArguments = std::tuple<>;
    ::ndk::ScopedAStatus handleStkCallSetupRequestFromSim(int32_t p_serial, bool p_accept) override;
    using HandleStkCallSetupRequestFromSimArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus hangup(int32_t p_serial, int32_t p_gsmIndex) override;
    using HangupArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus hangupForegroundResumeBackground(int32_t p_serial) override;
    using HangupForegroundResumeBackgroundArguments = std::tuple<>;
    ::ndk::ScopedAStatus hangupWaitingOrBackground(int32_t p_serial) override;
    using HangupWaitingOrBackgroundArguments = std::tuple<>;
    ::ndk::ScopedAStatus isVoNrEnabled(int32_t p_serial) override;
    using IsVoNrEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus rejectCall(int32_t p_serial) override;
    using RejectCallArguments = std::tuple<>;
    ::ndk::ScopedAStatus responseAcknowledgement() override;
    ::ndk::ScopedAStatus sendBurstDtmf(int32_t p_serial, const std::string& p_dtmf, int32_t p_on, int32_t p_off) override;
    using SendBurstDtmfArguments = std::tuple<std::string, int32_t, int32_t>;
    ::ndk::ScopedAStatus sendCdmaFeatureCode(int32_t p_serial, const std::string& p_featureCode) override;
    using SendCdmaFeatureCodeArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus sendDtmf(int32_t p_serial, const std::string& p_s) override;
    using SendDtmfArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus sendUssd(int32_t p_serial, const std::string& p_ussd) override;
    using SendUssdArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus separateConnection(int32_t p_serial, int32_t p_gsmIndex) override;
    using SeparateConnectionArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus setCallForward(int32_t p_serial, const ::aidl::android::hardware::radio::voice::CallForwardInfo& p_callInfo) override;
    using SetCallForwardArguments = std::tuple<::aidl::android::hardware::radio::voice::CallForwardInfo>;
    ::ndk::ScopedAStatus setCallWaiting(int32_t p_serial, bool p_enable, int32_t p_serviceClass) override;
    using SetCallWaitingArguments = std::tuple<bool, int32_t>;
    ::ndk::ScopedAStatus setClir(int32_t p_serial, int32_t p_status) override;
    using SetClirArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus setMute(int32_t p_serial, bool p_enable) override;
    using SetMuteArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setPreferredVoicePrivacy(int32_t p_serial, bool p_enable) override;
    using SetPreferredVoicePrivacyArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>& p_radioVoiceResponse, const std::shared_ptr<::aidl::android::hardware::radio::voice::IRadioVoiceIndication>& p_radioVoiceIndication) override;
    ::ndk::ScopedAStatus setTtyMode(int32_t p_serial, ::aidl::android::hardware::radio::voice::TtyMode p_mode) override;
    using SetTtyModeArguments = std::tuple<::aidl::android::hardware::radio::voice::TtyMode>;
    ::ndk::ScopedAStatus setVoNrEnabled(int32_t p_serial, bool p_enable) override;
    using SetVoNrEnabledArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus startDtmf(int32_t p_serial, const std::string& p_s) override;
    using StartDtmfArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus stopDtmf(int32_t p_serial) override;
    using StopDtmfArguments = std::tuple<>;
    ::ndk::ScopedAStatus switchWaitingOrHoldingAndActive(int32_t p_serial) override;
    using SwitchWaitingOrHoldingAndActiveArguments = std::tuple<>;
private:
    std::shared_ptr<Edge> edge_;
};
}
