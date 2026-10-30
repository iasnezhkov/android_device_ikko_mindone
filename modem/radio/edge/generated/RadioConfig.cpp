/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioConfig.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioConfig::getHalDeviceCapabilities(int32_t p_serial) {
    return edge_->request(Service::Config, Method::ConfigGetHalDeviceCapabilities, p_serial,
        std::make_any<GetHalDeviceCapabilitiesArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->getHalDeviceCapabilitiesResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioConfig::getNumOfLiveModems(int32_t p_serial) {
    return edge_->request(Service::Config, Method::ConfigGetNumOfLiveModems, p_serial,
        std::make_any<GetNumOfLiveModemsArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->getNumOfLiveModemsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioConfig::getPhoneCapability(int32_t p_serial) {
    return edge_->request(Service::Config, Method::ConfigGetPhoneCapability, p_serial,
        std::make_any<GetPhoneCapabilityArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->getPhoneCapabilityResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioConfig::getSimSlotsStatus(int32_t p_serial) {
    return edge_->request(Service::Config, Method::ConfigGetSimSlotsStatus, p_serial,
        std::make_any<GetSimSlotsStatusArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->getSimSlotsStatusResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioConfig::setNumOfLiveModems(int32_t p_serial, int8_t p_numOfLiveModems) {
    return edge_->request(Service::Config, Method::ConfigSetNumOfLiveModems, p_serial,
        std::make_any<SetNumOfLiveModemsArguments>(p_numOfLiveModems),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->setNumOfLiveModemsResponse(info);
        });
}
::ndk::ScopedAStatus RadioConfig::setPreferredDataModem(int32_t p_serial, int8_t p_modemId) {
    return edge_->request(Service::Config, Method::ConfigSetPreferredDataModem, p_serial,
        std::make_any<SetPreferredDataModemArguments>(p_modemId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->setPreferredDataModemResponse(info);
        });
}
::ndk::ScopedAStatus RadioConfig::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::config::IRadioConfigResponse>& p_radioConfigResponse, const std::shared_ptr<::aidl::android::hardware::radio::config::IRadioConfigIndication>& p_radioConfigIndication) {
    edge_->replace(Service::Config, p_radioConfigResponse, p_radioConfigIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioConfig::setSimSlotsMapping(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::config::SlotPortMapping>& p_slotMap) {
    return edge_->request(Service::Config, Method::ConfigSetSimSlotsMapping, p_serial,
        std::make_any<SetSimSlotsMappingArguments>(p_slotMap),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->setSimSlotsMappingResponse(info);
        });
}
::ndk::ScopedAStatus RadioConfig::getSimultaneousCallingSupport(int32_t p_serial) {
    return edge_->request(Service::Config, Method::ConfigGetSimultaneousCallingSupport, p_serial,
        std::make_any<GetSimultaneousCallingSupportArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->getSimultaneousCallingSupportResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioConfig::getSimTypeInfo(int32_t p_serial) {
    return edge_->request(Service::Config, Method::ConfigGetSimTypeInfo, p_serial,
        std::make_any<GetSimTypeInfoArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->getSimTypeInfoResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioConfig::setSimType(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::config::SimType>& p_simTypes) {
    return edge_->request(Service::Config, Method::ConfigSetSimType, p_serial,
        std::make_any<SetSimTypeArguments>(p_simTypes),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            return callback->setSimTypeResponse(info);
        });
}
}
