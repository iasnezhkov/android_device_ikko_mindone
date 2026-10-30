/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioData.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioData::allocatePduSessionId(int32_t p_serial) {
    return edge_->request(Service::Data, Method::DataAllocatePduSessionId, p_serial,
        std::make_any<AllocatePduSessionIdArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->allocatePduSessionIdResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioData::cancelHandover(int32_t p_serial, int32_t p_callId) {
    return edge_->request(Service::Data, Method::DataCancelHandover, p_serial,
        std::make_any<CancelHandoverArguments>(p_callId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->cancelHandoverResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::deactivateDataCall(int32_t p_serial, int32_t p_cid, ::aidl::android::hardware::radio::data::DataRequestReason p_reason) {
    return edge_->request(Service::Data, Method::DataDeactivateDataCall, p_serial,
        std::make_any<DeactivateDataCallArguments>(p_cid, p_reason),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->deactivateDataCallResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::getDataCallList(int32_t p_serial) {
    return edge_->request(Service::Data, Method::DataGetDataCallList, p_serial,
        std::make_any<GetDataCallListArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->getDataCallListResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioData::getSlicingConfig(int32_t p_serial) {
    return edge_->request(Service::Data, Method::DataGetSlicingConfig, p_serial,
        std::make_any<GetSlicingConfigArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->getSlicingConfigResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioData::releasePduSessionId(int32_t p_serial, int32_t p_id) {
    return edge_->request(Service::Data, Method::DataReleasePduSessionId, p_serial,
        std::make_any<ReleasePduSessionIdArguments>(p_id),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->releasePduSessionIdResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::responseAcknowledgement() {
    edge_->acknowledge(Service::Data);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioData::setDataAllowed(int32_t p_serial, bool p_allow) {
    return edge_->request(Service::Data, Method::DataSetDataAllowed, p_serial,
        std::make_any<SetDataAllowedArguments>(p_allow),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->setDataAllowedResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::setDataProfile(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::data::DataProfileInfo>& p_profiles) {
    return edge_->request(Service::Data, Method::DataSetDataProfile, p_serial,
        std::make_any<SetDataProfileArguments>(p_profiles),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->setDataProfileResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::setDataThrottling(int32_t p_serial, ::aidl::android::hardware::radio::data::DataThrottlingAction p_dataThrottlingAction, int64_t p_completionDurationMillis) {
    return edge_->request(Service::Data, Method::DataSetDataThrottling, p_serial,
        std::make_any<SetDataThrottlingArguments>(p_dataThrottlingAction, p_completionDurationMillis),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->setDataThrottlingResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::setInitialAttachApn(int32_t p_serial, const std::optional<::aidl::android::hardware::radio::data::DataProfileInfo>& p_dataProfileInfo) {
    return edge_->request(Service::Data, Method::DataSetInitialAttachApn, p_serial,
        std::make_any<SetInitialAttachApnArguments>(p_dataProfileInfo),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->setInitialAttachApnResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::data::IRadioDataResponse>& p_radioDataResponse, const std::shared_ptr<::aidl::android::hardware::radio::data::IRadioDataIndication>& p_radioDataIndication) {
    edge_->replace(Service::Data, p_radioDataResponse, p_radioDataIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioData::setupDataCall(int32_t p_serial, ::aidl::android::hardware::radio::AccessNetwork p_accessNetwork, const ::aidl::android::hardware::radio::data::DataProfileInfo& p_dataProfileInfo, bool p_roamingAllowed, ::aidl::android::hardware::radio::data::DataRequestReason p_reason, const std::vector<::aidl::android::hardware::radio::data::LinkAddress>& p_addresses, const std::vector<std::string>& p_dnses, int32_t p_pduSessionId, const std::optional<::aidl::android::hardware::radio::data::SliceInfo>& p_sliceInfo, bool p_matchAllRuleAllowed) {
    return edge_->request(Service::Data, Method::DataSetupDataCall, p_serial,
        std::make_any<SetupDataCallArguments>(p_accessNetwork, p_dataProfileInfo, p_roamingAllowed, p_reason, p_addresses, p_dnses, p_pduSessionId, p_sliceInfo, p_matchAllRuleAllowed),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->setupDataCallResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioData::startHandover(int32_t p_serial, int32_t p_callId) {
    return edge_->request(Service::Data, Method::DataStartHandover, p_serial,
        std::make_any<StartHandoverArguments>(p_callId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->startHandoverResponse(info);
        });
}
::ndk::ScopedAStatus RadioData::startKeepalive(int32_t p_serial, const ::aidl::android::hardware::radio::data::KeepaliveRequest& p_keepalive) {
    return edge_->request(Service::Data, Method::DataStartKeepalive, p_serial,
        std::make_any<StartKeepaliveArguments>(p_keepalive),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->startKeepaliveResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioData::stopKeepalive(int32_t p_serial, int32_t p_sessionHandle) {
    return edge_->request(Service::Data, Method::DataStopKeepalive, p_serial,
        std::make_any<StopKeepaliveArguments>(p_sessionHandle),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            return callback->stopKeepaliveResponse(info);
        });
}
}
