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
#include <aidl/android/hardware/radio/ims/BnRadioIms.h>
#include <aidl/android/hardware/radio/ims/EpsFallbackReason.h>
#include <aidl/android/hardware/radio/ims/IRadioImsIndication.h>
#include <aidl/android/hardware/radio/ims/IRadioImsResponse.h>
#include <aidl/android/hardware/radio/ims/ImsCall.h>
#include <aidl/android/hardware/radio/ims/ImsRegistration.h>
#include <aidl/android/hardware/radio/ims/ImsStreamDirection.h>
#include <aidl/android/hardware/radio/ims/ImsStreamType.h>
#include <aidl/android/hardware/radio/ims/ImsTrafficType.h>
#include <aidl/android/hardware/radio/ims/SrvccCall.h>
namespace mindone::radio {
class RadioIms final : public ::aidl::android::hardware::radio::ims::BnRadioIms {
public:
    explicit RadioIms(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus setSrvccCallInfo(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::ims::SrvccCall>& p_srvccCalls) override;
    using SetSrvccCallInfoArguments = std::tuple<std::vector<::aidl::android::hardware::radio::ims::SrvccCall>>;
    ::ndk::ScopedAStatus updateImsRegistrationInfo(int32_t p_serial, const ::aidl::android::hardware::radio::ims::ImsRegistration& p_imsRegistration) override;
    using UpdateImsRegistrationInfoArguments = std::tuple<::aidl::android::hardware::radio::ims::ImsRegistration>;
    ::ndk::ScopedAStatus startImsTraffic(int32_t p_serial, int32_t p_token, ::aidl::android::hardware::radio::ims::ImsTrafficType p_imsTrafficType, ::aidl::android::hardware::radio::AccessNetwork p_accessNetworkType, ::aidl::android::hardware::radio::ims::ImsCall::Direction p_trafficDirection) override;
    using StartImsTrafficArguments = std::tuple<int32_t, ::aidl::android::hardware::radio::ims::ImsTrafficType, ::aidl::android::hardware::radio::AccessNetwork, ::aidl::android::hardware::radio::ims::ImsCall::Direction>;
    ::ndk::ScopedAStatus stopImsTraffic(int32_t p_serial, int32_t p_token) override;
    using StopImsTrafficArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus triggerEpsFallback(int32_t p_serial, ::aidl::android::hardware::radio::ims::EpsFallbackReason p_reason) override;
    using TriggerEpsFallbackArguments = std::tuple<::aidl::android::hardware::radio::ims::EpsFallbackReason>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::ims::IRadioImsResponse>& p_radioImsResponse, const std::shared_ptr<::aidl::android::hardware::radio::ims::IRadioImsIndication>& p_radioImsIndication) override;
    ::ndk::ScopedAStatus sendAnbrQuery(int32_t p_serial, ::aidl::android::hardware::radio::ims::ImsStreamType p_mediaType, ::aidl::android::hardware::radio::ims::ImsStreamDirection p_direction, int32_t p_bitsPerSecond) override;
    using SendAnbrQueryArguments = std::tuple<::aidl::android::hardware::radio::ims::ImsStreamType, ::aidl::android::hardware::radio::ims::ImsStreamDirection, int32_t>;
    ::ndk::ScopedAStatus updateImsCallStatus(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::ims::ImsCall>& p_imsCalls) override;
    using UpdateImsCallStatusArguments = std::tuple<std::vector<::aidl::android::hardware::radio::ims::ImsCall>>;
private:
    std::shared_ptr<Edge> edge_;
};
}
