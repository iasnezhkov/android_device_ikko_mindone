/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioSim.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioSim::areUiccApplicationsEnabled(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimAreUiccApplicationsEnabled, p_serial,
        std::make_any<AreUiccApplicationsEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->areUiccApplicationsEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::changeIccPin2ForApp(int32_t p_serial, const std::string& p_oldPin2, const std::string& p_newPin2, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimChangeIccPin2ForApp, p_serial,
        std::make_any<ChangeIccPin2ForAppArguments>(p_oldPin2, p_newPin2, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->changeIccPin2ForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::changeIccPinForApp(int32_t p_serial, const std::string& p_oldPin, const std::string& p_newPin, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimChangeIccPinForApp, p_serial,
        std::make_any<ChangeIccPinForAppArguments>(p_oldPin, p_newPin, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->changeIccPinForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::enableUiccApplications(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Sim, Method::SimEnableUiccApplications, p_serial,
        std::make_any<EnableUiccApplicationsArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->enableUiccApplicationsResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::getAllowedCarriers(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimGetAllowedCarriers, p_serial,
        std::make_any<GetAllowedCarriersArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getAllowedCarriersResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioSim::getCdmaSubscription(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimGetCdmaSubscription, p_serial,
        std::make_any<GetCdmaSubscriptionArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getCdmaSubscriptionResponse(info, {}, {}, {}, {}, {});
        });
}
::ndk::ScopedAStatus RadioSim::getCdmaSubscriptionSource(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimGetCdmaSubscriptionSource, p_serial,
        std::make_any<GetCdmaSubscriptionSourceArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getCdmaSubscriptionSourceResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::getFacilityLockForApp(int32_t p_serial, const std::string& p_facility, const std::string& p_password, int32_t p_serviceClass, const std::string& p_appId) {
    return edge_->request(Service::Sim, Method::SimGetFacilityLockForApp, p_serial,
        std::make_any<GetFacilityLockForAppArguments>(p_facility, p_password, p_serviceClass, p_appId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getFacilityLockForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::getIccCardStatus(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimGetIccCardStatus, p_serial,
        std::make_any<GetIccCardStatusArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getIccCardStatusResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::getImsiForApp(int32_t p_serial, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimGetImsiForApp, p_serial,
        std::make_any<GetImsiForAppArguments>(p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getImsiForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::getSimPhonebookCapacity(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimGetSimPhonebookCapacity, p_serial,
        std::make_any<GetSimPhonebookCapacityArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getSimPhonebookCapacityResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::getSimPhonebookRecords(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimGetSimPhonebookRecords, p_serial,
        std::make_any<GetSimPhonebookRecordsArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->getSimPhonebookRecordsResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::iccCloseLogicalChannel(int32_t p_serial, int32_t p_channelId) {
    return edge_->request(Service::Sim, Method::SimIccCloseLogicalChannel, p_serial,
        std::make_any<IccCloseLogicalChannelArguments>(p_channelId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->iccCloseLogicalChannelResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::iccIoForApp(int32_t p_serial, const ::aidl::android::hardware::radio::sim::IccIo& p_iccIo) {
    return edge_->request(Service::Sim, Method::SimIccIoForApp, p_serial,
        std::make_any<IccIoForAppArguments>(p_iccIo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->iccIoForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::iccOpenLogicalChannel(int32_t p_serial, const std::string& p_aid, int32_t p_p2) {
    return edge_->request(Service::Sim, Method::SimIccOpenLogicalChannel, p_serial,
        std::make_any<IccOpenLogicalChannelArguments>(p_aid, p_p2),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->iccOpenLogicalChannelResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioSim::iccTransmitApduBasicChannel(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SimApdu& p_message) {
    return edge_->request(Service::Sim, Method::SimIccTransmitApduBasicChannel, p_serial,
        std::make_any<IccTransmitApduBasicChannelArguments>(p_message),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->iccTransmitApduBasicChannelResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::iccTransmitApduLogicalChannel(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SimApdu& p_message) {
    return edge_->request(Service::Sim, Method::SimIccTransmitApduLogicalChannel, p_serial,
        std::make_any<IccTransmitApduLogicalChannelArguments>(p_message),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->iccTransmitApduLogicalChannelResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::reportStkServiceIsRunning(int32_t p_serial) {
    return edge_->request(Service::Sim, Method::SimReportStkServiceIsRunning, p_serial,
        std::make_any<ReportStkServiceIsRunningArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->reportStkServiceIsRunningResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::requestIccSimAuthentication(int32_t p_serial, int32_t p_authContext, const std::string& p_authData, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimRequestIccSimAuthentication, p_serial,
        std::make_any<RequestIccSimAuthenticationArguments>(p_authContext, p_authData, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->requestIccSimAuthenticationResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::responseAcknowledgement() {
    edge_->acknowledge(Service::Sim);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioSim::sendEnvelope(int32_t p_serial, const std::string& p_contents) {
    return edge_->request(Service::Sim, Method::SimSendEnvelope, p_serial,
        std::make_any<SendEnvelopeArguments>(p_contents),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->sendEnvelopeResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::sendEnvelopeWithStatus(int32_t p_serial, const std::string& p_contents) {
    return edge_->request(Service::Sim, Method::SimSendEnvelopeWithStatus, p_serial,
        std::make_any<SendEnvelopeWithStatusArguments>(p_contents),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->sendEnvelopeWithStatusResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::sendTerminalResponseToSim(int32_t p_serial, const std::string& p_contents) {
    return edge_->request(Service::Sim, Method::SimSendTerminalResponseToSim, p_serial,
        std::make_any<SendTerminalResponseToSimArguments>(p_contents),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->sendTerminalResponseToSimResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::setAllowedCarriers(int32_t p_serial, const ::aidl::android::hardware::radio::sim::CarrierRestrictions& p_carriers, ::aidl::android::hardware::radio::sim::SimLockMultiSimPolicy p_multiSimPolicy) {
    return edge_->request(Service::Sim, Method::SimSetAllowedCarriers, p_serial,
        std::make_any<SetAllowedCarriersArguments>(p_carriers, p_multiSimPolicy),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->setAllowedCarriersResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::setCarrierInfoForImsiEncryption(int32_t p_serial, const ::aidl::android::hardware::radio::sim::ImsiEncryptionInfo& p_imsiEncryptionInfo) {
    return edge_->request(Service::Sim, Method::SimSetCarrierInfoForImsiEncryption, p_serial,
        std::make_any<SetCarrierInfoForImsiEncryptionArguments>(p_imsiEncryptionInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->setCarrierInfoForImsiEncryptionResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::setCdmaSubscriptionSource(int32_t p_serial, ::aidl::android::hardware::radio::sim::CdmaSubscriptionSource p_cdmaSub) {
    return edge_->request(Service::Sim, Method::SimSetCdmaSubscriptionSource, p_serial,
        std::make_any<SetCdmaSubscriptionSourceArguments>(p_cdmaSub),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->setCdmaSubscriptionSourceResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::setFacilityLockForApp(int32_t p_serial, const std::string& p_facility, bool p_lockState, const std::string& p_password, int32_t p_serviceClass, const std::string& p_appId) {
    return edge_->request(Service::Sim, Method::SimSetFacilityLockForApp, p_serial,
        std::make_any<SetFacilityLockForAppArguments>(p_facility, p_lockState, p_password, p_serviceClass, p_appId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->setFacilityLockForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::sim::IRadioSimResponse>& p_radioSimResponse, const std::shared_ptr<::aidl::android::hardware::radio::sim::IRadioSimIndication>& p_radioSimIndication) {
    edge_->replace(Service::Sim, p_radioSimResponse, p_radioSimIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioSim::setSimCardPower(int32_t p_serial, ::aidl::android::hardware::radio::sim::CardPowerState p_powerUp) {
    return edge_->request(Service::Sim, Method::SimSetSimCardPower, p_serial,
        std::make_any<SetSimCardPowerArguments>(p_powerUp),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->setSimCardPowerResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::setUiccSubscription(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SelectUiccSub& p_uiccSub) {
    return edge_->request(Service::Sim, Method::SimSetUiccSubscription, p_serial,
        std::make_any<SetUiccSubscriptionArguments>(p_uiccSub),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->setUiccSubscriptionResponse(info);
        });
}
::ndk::ScopedAStatus RadioSim::supplyIccPin2ForApp(int32_t p_serial, const std::string& p_pin2, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimSupplyIccPin2ForApp, p_serial,
        std::make_any<SupplyIccPin2ForAppArguments>(p_pin2, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->supplyIccPin2ForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::supplyIccPinForApp(int32_t p_serial, const std::string& p_pin, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimSupplyIccPinForApp, p_serial,
        std::make_any<SupplyIccPinForAppArguments>(p_pin, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->supplyIccPinForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::supplyIccPuk2ForApp(int32_t p_serial, const std::string& p_puk2, const std::string& p_pin2, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimSupplyIccPuk2ForApp, p_serial,
        std::make_any<SupplyIccPuk2ForAppArguments>(p_puk2, p_pin2, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->supplyIccPuk2ForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::supplyIccPukForApp(int32_t p_serial, const std::string& p_puk, const std::string& p_pin, const std::string& p_aid) {
    return edge_->request(Service::Sim, Method::SimSupplyIccPukForApp, p_serial,
        std::make_any<SupplyIccPukForAppArguments>(p_puk, p_pin, p_aid),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->supplyIccPukForAppResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::supplySimDepersonalization(int32_t p_serial, ::aidl::android::hardware::radio::sim::PersoSubstate p_persoType, const std::string& p_controlKey) {
    return edge_->request(Service::Sim, Method::SimSupplySimDepersonalization, p_serial,
        std::make_any<SupplySimDepersonalizationArguments>(p_persoType, p_controlKey),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->supplySimDepersonalizationResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioSim::updateSimPhonebookRecords(int32_t p_serial, const ::aidl::android::hardware::radio::sim::PhonebookRecordInfo& p_recordInfo) {
    return edge_->request(Service::Sim, Method::SimUpdateSimPhonebookRecords, p_serial,
        std::make_any<UpdateSimPhonebookRecordsArguments>(p_recordInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->updateSimPhonebookRecordsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioSim::iccCloseLogicalChannelWithSessionInfo(int32_t p_serial, const ::aidl::android::hardware::radio::sim::SessionInfo& p_sessionInfo) {
    return edge_->request(Service::Sim, Method::SimIccCloseLogicalChannelWithSessionInfo, p_serial,
        std::make_any<IccCloseLogicalChannelWithSessionInfoArguments>(p_sessionInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            return callback->iccCloseLogicalChannelWithSessionInfoResponse(info);
        });
}
}
