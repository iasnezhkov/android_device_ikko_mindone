/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioIms.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioIms::setSrvccCallInfo(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::ims::SrvccCall>& p_srvccCalls) {
    return edge_->request(Service::Ims, Method::ImsSetSrvccCallInfo, p_serial,
        std::make_any<SetSrvccCallInfoArguments>(p_srvccCalls),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->setSrvccCallInfoResponse(info);
        });
}
::ndk::ScopedAStatus RadioIms::updateImsRegistrationInfo(int32_t p_serial, const ::aidl::android::hardware::radio::ims::ImsRegistration& p_imsRegistration) {
    return edge_->request(Service::Ims, Method::ImsUpdateImsRegistrationInfo, p_serial,
        std::make_any<UpdateImsRegistrationInfoArguments>(p_imsRegistration),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->updateImsRegistrationInfoResponse(info);
        });
}
::ndk::ScopedAStatus RadioIms::startImsTraffic(int32_t p_serial, int32_t p_token, ::aidl::android::hardware::radio::ims::ImsTrafficType p_imsTrafficType, ::aidl::android::hardware::radio::AccessNetwork p_accessNetworkType, ::aidl::android::hardware::radio::ims::ImsCall::Direction p_trafficDirection) {
    return edge_->request(Service::Ims, Method::ImsStartImsTraffic, p_serial,
        std::make_any<StartImsTrafficArguments>(p_token, p_imsTrafficType, p_accessNetworkType, p_trafficDirection),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->startImsTrafficResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioIms::stopImsTraffic(int32_t p_serial, int32_t p_token) {
    return edge_->request(Service::Ims, Method::ImsStopImsTraffic, p_serial,
        std::make_any<StopImsTrafficArguments>(p_token),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->stopImsTrafficResponse(info);
        });
}
::ndk::ScopedAStatus RadioIms::triggerEpsFallback(int32_t p_serial, ::aidl::android::hardware::radio::ims::EpsFallbackReason p_reason) {
    return edge_->request(Service::Ims, Method::ImsTriggerEpsFallback, p_serial,
        std::make_any<TriggerEpsFallbackArguments>(p_reason),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->triggerEpsFallbackResponse(info);
        });
}
::ndk::ScopedAStatus RadioIms::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::ims::IRadioImsResponse>& p_radioImsResponse, const std::shared_ptr<::aidl::android::hardware::radio::ims::IRadioImsIndication>& p_radioImsIndication) {
    edge_->replace(Service::Ims, p_radioImsResponse, p_radioImsIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioIms::sendAnbrQuery(int32_t p_serial, ::aidl::android::hardware::radio::ims::ImsStreamType p_mediaType, ::aidl::android::hardware::radio::ims::ImsStreamDirection p_direction, int32_t p_bitsPerSecond) {
    return edge_->request(Service::Ims, Method::ImsSendAnbrQuery, p_serial,
        std::make_any<SendAnbrQueryArguments>(p_mediaType, p_direction, p_bitsPerSecond),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->sendAnbrQueryResponse(info);
        });
}
::ndk::ScopedAStatus RadioIms::updateImsCallStatus(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::ims::ImsCall>& p_imsCalls) {
    return edge_->request(Service::Ims, Method::ImsUpdateImsCallStatus, p_serial,
        std::make_any<UpdateImsCallStatusArguments>(p_imsCalls),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::ims::IRadioImsResponse>(snapshot.response);
            return callback->updateImsCallStatusResponse(info);
        });
}
}
