/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioVoice.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioVoice::acceptCall(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceAcceptCall, p_serial,
        std::make_any<AcceptCallArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->acceptCallResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::cancelPendingUssd(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceCancelPendingUssd, p_serial,
        std::make_any<CancelPendingUssdArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->cancelPendingUssdResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::conference(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceConference, p_serial,
        std::make_any<ConferenceArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->conferenceResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::dial(int32_t p_serial, const ::aidl::android::hardware::radio::voice::Dial& p_dialInfo) {
    return edge_->request(Service::Voice, Method::VoiceDial, p_serial,
        std::make_any<DialArguments>(p_dialInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->dialResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::emergencyDial(int32_t p_serial, const ::aidl::android::hardware::radio::voice::Dial& p_dialInfo, int32_t p_categories, const std::vector<std::string>& p_urns, ::aidl::android::hardware::radio::voice::EmergencyCallRouting p_routing, bool p_hasKnownUserIntentEmergency, bool p_isTesting) {
    return edge_->request(Service::Voice, Method::VoiceEmergencyDial, p_serial,
        std::make_any<EmergencyDialArguments>(p_dialInfo, p_categories, p_urns, p_routing, p_hasKnownUserIntentEmergency, p_isTesting),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->emergencyDialResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::exitEmergencyCallbackMode(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceExitEmergencyCallbackMode, p_serial,
        std::make_any<ExitEmergencyCallbackModeArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->exitEmergencyCallbackModeResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::explicitCallTransfer(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceExplicitCallTransfer, p_serial,
        std::make_any<ExplicitCallTransferArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->explicitCallTransferResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::getCallForwardStatus(int32_t p_serial, const ::aidl::android::hardware::radio::voice::CallForwardInfo& p_callInfo) {
    return edge_->request(Service::Voice, Method::VoiceGetCallForwardStatus, p_serial,
        std::make_any<GetCallForwardStatusArguments>(p_callInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getCallForwardStatusResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getCallWaiting(int32_t p_serial, int32_t p_serviceClass) {
    return edge_->request(Service::Voice, Method::VoiceGetCallWaiting, p_serial,
        std::make_any<GetCallWaitingArguments>(p_serviceClass),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getCallWaitingResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getClip(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetClip, p_serial,
        std::make_any<GetClipArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getClipResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getClir(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetClir, p_serial,
        std::make_any<GetClirArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getClirResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getCurrentCalls(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetCurrentCalls, p_serial,
        std::make_any<GetCurrentCallsArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getCurrentCallsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getLastCallFailCause(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetLastCallFailCause, p_serial,
        std::make_any<GetLastCallFailCauseArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getLastCallFailCauseResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getMute(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetMute, p_serial,
        std::make_any<GetMuteArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getMuteResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getPreferredVoicePrivacy(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetPreferredVoicePrivacy, p_serial,
        std::make_any<GetPreferredVoicePrivacyArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getPreferredVoicePrivacyResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::getTtyMode(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceGetTtyMode, p_serial,
        std::make_any<GetTtyModeArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->getTtyModeResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::handleStkCallSetupRequestFromSim(int32_t p_serial, bool p_accept) {
    return edge_->request(Service::Voice, Method::VoiceHandleStkCallSetupRequestFromSim, p_serial,
        std::make_any<HandleStkCallSetupRequestFromSimArguments>(p_accept),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->handleStkCallSetupRequestFromSimResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::hangup(int32_t p_serial, int32_t p_gsmIndex) {
    return edge_->request(Service::Voice, Method::VoiceHangup, p_serial,
        std::make_any<HangupArguments>(p_gsmIndex),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->hangupConnectionResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::hangupForegroundResumeBackground(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceHangupForegroundResumeBackground, p_serial,
        std::make_any<HangupForegroundResumeBackgroundArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->hangupForegroundResumeBackgroundResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::hangupWaitingOrBackground(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceHangupWaitingOrBackground, p_serial,
        std::make_any<HangupWaitingOrBackgroundArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->hangupWaitingOrBackgroundResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::isVoNrEnabled(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceIsVoNrEnabled, p_serial,
        std::make_any<IsVoNrEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->isVoNrEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioVoice::rejectCall(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceRejectCall, p_serial,
        std::make_any<RejectCallArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->rejectCallResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::responseAcknowledgement() {
    edge_->acknowledge(Service::Voice);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioVoice::sendBurstDtmf(int32_t p_serial, const std::string& p_dtmf, int32_t p_on, int32_t p_off) {
    return edge_->request(Service::Voice, Method::VoiceSendBurstDtmf, p_serial,
        std::make_any<SendBurstDtmfArguments>(p_dtmf, p_on, p_off),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->sendBurstDtmfResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::sendCdmaFeatureCode(int32_t p_serial, const std::string& p_featureCode) {
    return edge_->request(Service::Voice, Method::VoiceSendCdmaFeatureCode, p_serial,
        std::make_any<SendCdmaFeatureCodeArguments>(p_featureCode),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->sendCdmaFeatureCodeResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::sendDtmf(int32_t p_serial, const std::string& p_s) {
    return edge_->request(Service::Voice, Method::VoiceSendDtmf, p_serial,
        std::make_any<SendDtmfArguments>(p_s),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->sendDtmfResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::sendUssd(int32_t p_serial, const std::string& p_ussd) {
    return edge_->request(Service::Voice, Method::VoiceSendUssd, p_serial,
        std::make_any<SendUssdArguments>(p_ussd),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->sendUssdResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::separateConnection(int32_t p_serial, int32_t p_gsmIndex) {
    return edge_->request(Service::Voice, Method::VoiceSeparateConnection, p_serial,
        std::make_any<SeparateConnectionArguments>(p_gsmIndex),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->separateConnectionResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setCallForward(int32_t p_serial, const ::aidl::android::hardware::radio::voice::CallForwardInfo& p_callInfo) {
    return edge_->request(Service::Voice, Method::VoiceSetCallForward, p_serial,
        std::make_any<SetCallForwardArguments>(p_callInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setCallForwardResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setCallWaiting(int32_t p_serial, bool p_enable, int32_t p_serviceClass) {
    return edge_->request(Service::Voice, Method::VoiceSetCallWaiting, p_serial,
        std::make_any<SetCallWaitingArguments>(p_enable, p_serviceClass),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setCallWaitingResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setClir(int32_t p_serial, int32_t p_status) {
    return edge_->request(Service::Voice, Method::VoiceSetClir, p_serial,
        std::make_any<SetClirArguments>(p_status),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setClirResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setMute(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Voice, Method::VoiceSetMute, p_serial,
        std::make_any<SetMuteArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setMuteResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setPreferredVoicePrivacy(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Voice, Method::VoiceSetPreferredVoicePrivacy, p_serial,
        std::make_any<SetPreferredVoicePrivacyArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setPreferredVoicePrivacyResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>& p_radioVoiceResponse, const std::shared_ptr<::aidl::android::hardware::radio::voice::IRadioVoiceIndication>& p_radioVoiceIndication) {
    edge_->replace(Service::Voice, p_radioVoiceResponse, p_radioVoiceIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioVoice::setTtyMode(int32_t p_serial, ::aidl::android::hardware::radio::voice::TtyMode p_mode) {
    return edge_->request(Service::Voice, Method::VoiceSetTtyMode, p_serial,
        std::make_any<SetTtyModeArguments>(p_mode),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setTtyModeResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::setVoNrEnabled(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Voice, Method::VoiceSetVoNrEnabled, p_serial,
        std::make_any<SetVoNrEnabledArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->setVoNrEnabledResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::startDtmf(int32_t p_serial, const std::string& p_s) {
    return edge_->request(Service::Voice, Method::VoiceStartDtmf, p_serial,
        std::make_any<StartDtmfArguments>(p_s),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->startDtmfResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::stopDtmf(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceStopDtmf, p_serial,
        std::make_any<StopDtmfArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->stopDtmfResponse(info);
        });
}
::ndk::ScopedAStatus RadioVoice::switchWaitingOrHoldingAndActive(int32_t p_serial) {
    return edge_->request(Service::Voice, Method::VoiceSwitchWaitingOrHoldingAndActive, p_serial,
        std::make_any<SwitchWaitingOrHoldingAndActiveArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            return callback->switchWaitingOrHoldingAndActiveResponse(info);
        });
}
}
