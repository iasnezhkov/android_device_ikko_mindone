/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioMessaging.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioMessaging::acknowledgeIncomingGsmSmsWithPdu(int32_t p_serial, bool p_success, const std::string& p_ackPdu) {
    return edge_->request(Service::Messaging, Method::MessagingAcknowledgeIncomingGsmSmsWithPdu, p_serial,
        std::make_any<AcknowledgeIncomingGsmSmsWithPduArguments>(p_success, p_ackPdu),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->acknowledgeIncomingGsmSmsWithPduResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::acknowledgeLastIncomingCdmaSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsAck& p_smsAck) {
    return edge_->request(Service::Messaging, Method::MessagingAcknowledgeLastIncomingCdmaSms, p_serial,
        std::make_any<AcknowledgeLastIncomingCdmaSmsArguments>(p_smsAck),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->acknowledgeLastIncomingCdmaSmsResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::acknowledgeLastIncomingGsmSms(int32_t p_serial, bool p_success, ::aidl::android::hardware::radio::messaging::SmsAcknowledgeFailCause p_cause) {
    return edge_->request(Service::Messaging, Method::MessagingAcknowledgeLastIncomingGsmSms, p_serial,
        std::make_any<AcknowledgeLastIncomingGsmSmsArguments>(p_success, p_cause),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->acknowledgeLastIncomingGsmSmsResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::deleteSmsOnRuim(int32_t p_serial, int32_t p_index) {
    return edge_->request(Service::Messaging, Method::MessagingDeleteSmsOnRuim, p_serial,
        std::make_any<DeleteSmsOnRuimArguments>(p_index),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->deleteSmsOnRuimResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::deleteSmsOnSim(int32_t p_serial, int32_t p_index) {
    return edge_->request(Service::Messaging, Method::MessagingDeleteSmsOnSim, p_serial,
        std::make_any<DeleteSmsOnSimArguments>(p_index),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->deleteSmsOnSimResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::getCdmaBroadcastConfig(int32_t p_serial) {
    return edge_->request(Service::Messaging, Method::MessagingGetCdmaBroadcastConfig, p_serial,
        std::make_any<GetCdmaBroadcastConfigArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->getCdmaBroadcastConfigResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::getGsmBroadcastConfig(int32_t p_serial) {
    return edge_->request(Service::Messaging, Method::MessagingGetGsmBroadcastConfig, p_serial,
        std::make_any<GetGsmBroadcastConfigArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->getGsmBroadcastConfigResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::getSmscAddress(int32_t p_serial) {
    return edge_->request(Service::Messaging, Method::MessagingGetSmscAddress, p_serial,
        std::make_any<GetSmscAddressArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->getSmscAddressResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::reportSmsMemoryStatus(int32_t p_serial, bool p_available) {
    return edge_->request(Service::Messaging, Method::MessagingReportSmsMemoryStatus, p_serial,
        std::make_any<ReportSmsMemoryStatusArguments>(p_available),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->reportSmsMemoryStatusResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::responseAcknowledgement() {
    edge_->acknowledge(Service::Messaging);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioMessaging::sendCdmaSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsMessage& p_sms) {
    return edge_->request(Service::Messaging, Method::MessagingSendCdmaSms, p_serial,
        std::make_any<SendCdmaSmsArguments>(p_sms),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->sendCdmaSmsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::sendCdmaSmsExpectMore(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsMessage& p_sms) {
    return edge_->request(Service::Messaging, Method::MessagingSendCdmaSmsExpectMore, p_serial,
        std::make_any<SendCdmaSmsExpectMoreArguments>(p_sms),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->sendCdmaSmsExpectMoreResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::sendImsSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::ImsSmsMessage& p_message) {
    return edge_->request(Service::Messaging, Method::MessagingSendImsSms, p_serial,
        std::make_any<SendImsSmsArguments>(p_message),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->sendImsSmsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::sendSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::GsmSmsMessage& p_message) {
    return edge_->request(Service::Messaging, Method::MessagingSendSms, p_serial,
        std::make_any<SendSmsArguments>(p_message),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->sendSmsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::sendSmsExpectMore(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::GsmSmsMessage& p_message) {
    return edge_->request(Service::Messaging, Method::MessagingSendSmsExpectMore, p_serial,
        std::make_any<SendSmsExpectMoreArguments>(p_message),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->sendSmsExpectMoreResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::setCdmaBroadcastActivation(int32_t p_serial, bool p_activate) {
    return edge_->request(Service::Messaging, Method::MessagingSetCdmaBroadcastActivation, p_serial,
        std::make_any<SetCdmaBroadcastActivationArguments>(p_activate),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->setCdmaBroadcastActivationResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::setCdmaBroadcastConfig(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::messaging::CdmaBroadcastSmsConfigInfo>& p_configInfo) {
    return edge_->request(Service::Messaging, Method::MessagingSetCdmaBroadcastConfig, p_serial,
        std::make_any<SetCdmaBroadcastConfigArguments>(p_configInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->setCdmaBroadcastConfigResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::setGsmBroadcastActivation(int32_t p_serial, bool p_activate) {
    return edge_->request(Service::Messaging, Method::MessagingSetGsmBroadcastActivation, p_serial,
        std::make_any<SetGsmBroadcastActivationArguments>(p_activate),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->setGsmBroadcastActivationResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::setGsmBroadcastConfig(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::messaging::GsmBroadcastSmsConfigInfo>& p_configInfo) {
    return edge_->request(Service::Messaging, Method::MessagingSetGsmBroadcastConfig, p_serial,
        std::make_any<SetGsmBroadcastConfigArguments>(p_configInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->setGsmBroadcastConfigResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>& p_radioMessagingResponse, const std::shared_ptr<::aidl::android::hardware::radio::messaging::IRadioMessagingIndication>& p_radioMessagingIndication) {
    edge_->replace(Service::Messaging, p_radioMessagingResponse, p_radioMessagingIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioMessaging::setSmscAddress(int32_t p_serial, const std::string& p_smsc) {
    return edge_->request(Service::Messaging, Method::MessagingSetSmscAddress, p_serial,
        std::make_any<SetSmscAddressArguments>(p_smsc),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->setSmscAddressResponse(info);
        });
}
::ndk::ScopedAStatus RadioMessaging::writeSmsToRuim(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsWriteArgs& p_cdmaSms) {
    return edge_->request(Service::Messaging, Method::MessagingWriteSmsToRuim, p_serial,
        std::make_any<WriteSmsToRuimArguments>(p_cdmaSms),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->writeSmsToRuimResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioMessaging::writeSmsToSim(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::SmsWriteArgs& p_smsWriteArgs) {
    return edge_->request(Service::Messaging, Method::MessagingWriteSmsToSim, p_serial,
        std::make_any<WriteSmsToSimArguments>(p_smsWriteArgs),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            return callback->writeSmsToSimResponse(info, {});
        });
}
}
