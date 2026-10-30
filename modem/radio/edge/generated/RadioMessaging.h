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
#include <aidl/android/hardware/radio/messaging/BnRadioMessaging.h>
#include <aidl/android/hardware/radio/messaging/CdmaBroadcastSmsConfigInfo.h>
#include <aidl/android/hardware/radio/messaging/CdmaSmsAck.h>
#include <aidl/android/hardware/radio/messaging/CdmaSmsMessage.h>
#include <aidl/android/hardware/radio/messaging/CdmaSmsWriteArgs.h>
#include <aidl/android/hardware/radio/messaging/GsmBroadcastSmsConfigInfo.h>
#include <aidl/android/hardware/radio/messaging/GsmSmsMessage.h>
#include <aidl/android/hardware/radio/messaging/IRadioMessagingIndication.h>
#include <aidl/android/hardware/radio/messaging/IRadioMessagingResponse.h>
#include <aidl/android/hardware/radio/messaging/ImsSmsMessage.h>
#include <aidl/android/hardware/radio/messaging/SmsAcknowledgeFailCause.h>
#include <aidl/android/hardware/radio/messaging/SmsWriteArgs.h>
namespace mindone::radio {
class RadioMessaging final : public ::aidl::android::hardware::radio::messaging::BnRadioMessaging {
public:
    explicit RadioMessaging(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus acknowledgeIncomingGsmSmsWithPdu(int32_t p_serial, bool p_success, const std::string& p_ackPdu) override;
    using AcknowledgeIncomingGsmSmsWithPduArguments = std::tuple<bool, std::string>;
    ::ndk::ScopedAStatus acknowledgeLastIncomingCdmaSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsAck& p_smsAck) override;
    using AcknowledgeLastIncomingCdmaSmsArguments = std::tuple<::aidl::android::hardware::radio::messaging::CdmaSmsAck>;
    ::ndk::ScopedAStatus acknowledgeLastIncomingGsmSms(int32_t p_serial, bool p_success, ::aidl::android::hardware::radio::messaging::SmsAcknowledgeFailCause p_cause) override;
    using AcknowledgeLastIncomingGsmSmsArguments = std::tuple<bool, ::aidl::android::hardware::radio::messaging::SmsAcknowledgeFailCause>;
    ::ndk::ScopedAStatus deleteSmsOnRuim(int32_t p_serial, int32_t p_index) override;
    using DeleteSmsOnRuimArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus deleteSmsOnSim(int32_t p_serial, int32_t p_index) override;
    using DeleteSmsOnSimArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus getCdmaBroadcastConfig(int32_t p_serial) override;
    using GetCdmaBroadcastConfigArguments = std::tuple<>;
    ::ndk::ScopedAStatus getGsmBroadcastConfig(int32_t p_serial) override;
    using GetGsmBroadcastConfigArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSmscAddress(int32_t p_serial) override;
    using GetSmscAddressArguments = std::tuple<>;
    ::ndk::ScopedAStatus reportSmsMemoryStatus(int32_t p_serial, bool p_available) override;
    using ReportSmsMemoryStatusArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus responseAcknowledgement() override;
    ::ndk::ScopedAStatus sendCdmaSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsMessage& p_sms) override;
    using SendCdmaSmsArguments = std::tuple<::aidl::android::hardware::radio::messaging::CdmaSmsMessage>;
    ::ndk::ScopedAStatus sendCdmaSmsExpectMore(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsMessage& p_sms) override;
    using SendCdmaSmsExpectMoreArguments = std::tuple<::aidl::android::hardware::radio::messaging::CdmaSmsMessage>;
    ::ndk::ScopedAStatus sendImsSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::ImsSmsMessage& p_message) override;
    using SendImsSmsArguments = std::tuple<::aidl::android::hardware::radio::messaging::ImsSmsMessage>;
    ::ndk::ScopedAStatus sendSms(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::GsmSmsMessage& p_message) override;
    using SendSmsArguments = std::tuple<::aidl::android::hardware::radio::messaging::GsmSmsMessage>;
    ::ndk::ScopedAStatus sendSmsExpectMore(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::GsmSmsMessage& p_message) override;
    using SendSmsExpectMoreArguments = std::tuple<::aidl::android::hardware::radio::messaging::GsmSmsMessage>;
    ::ndk::ScopedAStatus setCdmaBroadcastActivation(int32_t p_serial, bool p_activate) override;
    using SetCdmaBroadcastActivationArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setCdmaBroadcastConfig(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::messaging::CdmaBroadcastSmsConfigInfo>& p_configInfo) override;
    using SetCdmaBroadcastConfigArguments = std::tuple<std::vector<::aidl::android::hardware::radio::messaging::CdmaBroadcastSmsConfigInfo>>;
    ::ndk::ScopedAStatus setGsmBroadcastActivation(int32_t p_serial, bool p_activate) override;
    using SetGsmBroadcastActivationArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setGsmBroadcastConfig(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::messaging::GsmBroadcastSmsConfigInfo>& p_configInfo) override;
    using SetGsmBroadcastConfigArguments = std::tuple<std::vector<::aidl::android::hardware::radio::messaging::GsmBroadcastSmsConfigInfo>>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>& p_radioMessagingResponse, const std::shared_ptr<::aidl::android::hardware::radio::messaging::IRadioMessagingIndication>& p_radioMessagingIndication) override;
    ::ndk::ScopedAStatus setSmscAddress(int32_t p_serial, const std::string& p_smsc) override;
    using SetSmscAddressArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus writeSmsToRuim(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::CdmaSmsWriteArgs& p_cdmaSms) override;
    using WriteSmsToRuimArguments = std::tuple<::aidl::android::hardware::radio::messaging::CdmaSmsWriteArgs>;
    ::ndk::ScopedAStatus writeSmsToSim(int32_t p_serial, const ::aidl::android::hardware::radio::messaging::SmsWriteArgs& p_smsWriteArgs) override;
    using WriteSmsToSimArguments = std::tuple<::aidl::android::hardware::radio::messaging::SmsWriteArgs>;
private:
    std::shared_ptr<Edge> edge_;
};
}
