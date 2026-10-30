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
#include <aidl/android/hardware/radio/AccessNetwork.h>
#include <aidl/android/hardware/radio/data/BnRadioData.h>
#include <aidl/android/hardware/radio/data/DataProfileInfo.h>
#include <aidl/android/hardware/radio/data/DataRequestReason.h>
#include <aidl/android/hardware/radio/data/DataThrottlingAction.h>
#include <aidl/android/hardware/radio/data/IRadioDataIndication.h>
#include <aidl/android/hardware/radio/data/IRadioDataResponse.h>
#include <aidl/android/hardware/radio/data/KeepaliveRequest.h>
#include <aidl/android/hardware/radio/data/LinkAddress.h>
#include <aidl/android/hardware/radio/data/SliceInfo.h>
namespace mindone::radio {
class RadioData final : public ::aidl::android::hardware::radio::data::BnRadioData {
public:
    explicit RadioData(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus allocatePduSessionId(int32_t p_serial) override;
    using AllocatePduSessionIdArguments = std::tuple<>;
    ::ndk::ScopedAStatus cancelHandover(int32_t p_serial, int32_t p_callId) override;
    using CancelHandoverArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus deactivateDataCall(int32_t p_serial, int32_t p_cid, ::aidl::android::hardware::radio::data::DataRequestReason p_reason) override;
    using DeactivateDataCallArguments = std::tuple<int32_t, ::aidl::android::hardware::radio::data::DataRequestReason>;
    ::ndk::ScopedAStatus getDataCallList(int32_t p_serial) override;
    using GetDataCallListArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSlicingConfig(int32_t p_serial) override;
    using GetSlicingConfigArguments = std::tuple<>;
    ::ndk::ScopedAStatus releasePduSessionId(int32_t p_serial, int32_t p_id) override;
    using ReleasePduSessionIdArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus responseAcknowledgement() override;
    ::ndk::ScopedAStatus setDataAllowed(int32_t p_serial, bool p_allow) override;
    using SetDataAllowedArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setDataProfile(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::data::DataProfileInfo>& p_profiles) override;
    using SetDataProfileArguments = std::tuple<std::vector<::aidl::android::hardware::radio::data::DataProfileInfo>>;
    ::ndk::ScopedAStatus setDataThrottling(int32_t p_serial, ::aidl::android::hardware::radio::data::DataThrottlingAction p_dataThrottlingAction, int64_t p_completionDurationMillis) override;
    using SetDataThrottlingArguments = std::tuple<::aidl::android::hardware::radio::data::DataThrottlingAction, int64_t>;
    ::ndk::ScopedAStatus setInitialAttachApn(int32_t p_serial, const std::optional<::aidl::android::hardware::radio::data::DataProfileInfo>& p_dataProfileInfo) override;
    using SetInitialAttachApnArguments = std::tuple<std::optional<::aidl::android::hardware::radio::data::DataProfileInfo>>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::data::IRadioDataResponse>& p_radioDataResponse, const std::shared_ptr<::aidl::android::hardware::radio::data::IRadioDataIndication>& p_radioDataIndication) override;
    ::ndk::ScopedAStatus setupDataCall(int32_t p_serial, ::aidl::android::hardware::radio::AccessNetwork p_accessNetwork, const ::aidl::android::hardware::radio::data::DataProfileInfo& p_dataProfileInfo, bool p_roamingAllowed, ::aidl::android::hardware::radio::data::DataRequestReason p_reason, const std::vector<::aidl::android::hardware::radio::data::LinkAddress>& p_addresses, const std::vector<std::string>& p_dnses, int32_t p_pduSessionId, const std::optional<::aidl::android::hardware::radio::data::SliceInfo>& p_sliceInfo, bool p_matchAllRuleAllowed) override;
    using SetupDataCallArguments = std::tuple<::aidl::android::hardware::radio::AccessNetwork, ::aidl::android::hardware::radio::data::DataProfileInfo, bool, ::aidl::android::hardware::radio::data::DataRequestReason, std::vector<::aidl::android::hardware::radio::data::LinkAddress>, std::vector<std::string>, int32_t, std::optional<::aidl::android::hardware::radio::data::SliceInfo>, bool>;
    ::ndk::ScopedAStatus startHandover(int32_t p_serial, int32_t p_callId) override;
    using StartHandoverArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus startKeepalive(int32_t p_serial, const ::aidl::android::hardware::radio::data::KeepaliveRequest& p_keepalive) override;
    using StartKeepaliveArguments = std::tuple<::aidl::android::hardware::radio::data::KeepaliveRequest>;
    ::ndk::ScopedAStatus stopKeepalive(int32_t p_serial, int32_t p_sessionHandle) override;
    using StopKeepaliveArguments = std::tuple<int32_t>;
private:
    std::shared_ptr<Edge> edge_;
};
}
