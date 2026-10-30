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
#include <aidl/android/hardware/radio/config/BnRadioConfig.h>
#include <aidl/android/hardware/radio/config/IRadioConfigIndication.h>
#include <aidl/android/hardware/radio/config/IRadioConfigResponse.h>
#include <aidl/android/hardware/radio/config/SimType.h>
#include <aidl/android/hardware/radio/config/SlotPortMapping.h>
namespace mindone::radio {
class RadioConfig final : public ::aidl::android::hardware::radio::config::BnRadioConfig {
public:
    explicit RadioConfig(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus getHalDeviceCapabilities(int32_t p_serial) override;
    using GetHalDeviceCapabilitiesArguments = std::tuple<>;
    ::ndk::ScopedAStatus getNumOfLiveModems(int32_t p_serial) override;
    using GetNumOfLiveModemsArguments = std::tuple<>;
    ::ndk::ScopedAStatus getPhoneCapability(int32_t p_serial) override;
    using GetPhoneCapabilityArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSimSlotsStatus(int32_t p_serial) override;
    using GetSimSlotsStatusArguments = std::tuple<>;
    ::ndk::ScopedAStatus setNumOfLiveModems(int32_t p_serial, int8_t p_numOfLiveModems) override;
    using SetNumOfLiveModemsArguments = std::tuple<int8_t>;
    ::ndk::ScopedAStatus setPreferredDataModem(int32_t p_serial, int8_t p_modemId) override;
    using SetPreferredDataModemArguments = std::tuple<int8_t>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::config::IRadioConfigResponse>& p_radioConfigResponse, const std::shared_ptr<::aidl::android::hardware::radio::config::IRadioConfigIndication>& p_radioConfigIndication) override;
    ::ndk::ScopedAStatus setSimSlotsMapping(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::config::SlotPortMapping>& p_slotMap) override;
    using SetSimSlotsMappingArguments = std::tuple<std::vector<::aidl::android::hardware::radio::config::SlotPortMapping>>;
    ::ndk::ScopedAStatus getSimultaneousCallingSupport(int32_t p_serial) override;
    using GetSimultaneousCallingSupportArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSimTypeInfo(int32_t p_serial) override;
    using GetSimTypeInfoArguments = std::tuple<>;
    ::ndk::ScopedAStatus setSimType(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::config::SimType>& p_simTypes) override;
    using SetSimTypeArguments = std::tuple<std::vector<::aidl::android::hardware::radio::config::SimType>>;
private:
    std::shared_ptr<Edge> edge_;
};
}
