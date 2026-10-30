/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioModem.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioModem::enableModem(int32_t p_serial, bool p_on) {
    return edge_->request(Service::Modem, Method::ModemEnableModem, p_serial,
        std::make_any<EnableModemArguments>(p_on),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->enableModemResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::getBasebandVersion(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetBasebandVersion, p_serial,
        std::make_any<GetBasebandVersionArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getBasebandVersionResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::getDeviceIdentity(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetDeviceIdentity, p_serial,
        std::make_any<GetDeviceIdentityArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getDeviceIdentityResponse(info, {}, {}, {}, {});
        });
}
::ndk::ScopedAStatus RadioModem::getHardwareConfig(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetHardwareConfig, p_serial,
        std::make_any<GetHardwareConfigArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getHardwareConfigResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::getModemActivityInfo(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetModemActivityInfo, p_serial,
        std::make_any<GetModemActivityInfoArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getModemActivityInfoResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::getModemStackStatus(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetModemStackStatus, p_serial,
        std::make_any<GetModemStackStatusArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getModemStackStatusResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::getRadioCapability(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetRadioCapability, p_serial,
        std::make_any<GetRadioCapabilityArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getRadioCapabilityResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::nvReadItem(int32_t p_serial, ::aidl::android::hardware::radio::modem::NvItem p_itemId) {
    return edge_->request(Service::Modem, Method::ModemNvReadItem, p_serial,
        std::make_any<NvReadItemArguments>(p_itemId),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->nvReadItemResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::nvResetConfig(int32_t p_serial, ::aidl::android::hardware::radio::modem::ResetNvType p_resetType) {
    return edge_->request(Service::Modem, Method::ModemNvResetConfig, p_serial,
        std::make_any<NvResetConfigArguments>(p_resetType),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->nvResetConfigResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::nvWriteCdmaPrl(int32_t p_serial, const std::vector<uint8_t>& p_prl) {
    return edge_->request(Service::Modem, Method::ModemNvWriteCdmaPrl, p_serial,
        std::make_any<NvWriteCdmaPrlArguments>(p_prl),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->nvWriteCdmaPrlResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::nvWriteItem(int32_t p_serial, const ::aidl::android::hardware::radio::modem::NvWriteItem& p_item) {
    return edge_->request(Service::Modem, Method::ModemNvWriteItem, p_serial,
        std::make_any<NvWriteItemArguments>(p_item),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->nvWriteItemResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::requestShutdown(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemRequestShutdown, p_serial,
        std::make_any<RequestShutdownArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->requestShutdownResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::responseAcknowledgement() {
    edge_->acknowledge(Service::Modem);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioModem::sendDeviceState(int32_t p_serial, ::aidl::android::hardware::radio::modem::DeviceStateType p_deviceStateType, bool p_state) {
    return edge_->request(Service::Modem, Method::ModemSendDeviceState, p_serial,
        std::make_any<SendDeviceStateArguments>(p_deviceStateType, p_state),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->sendDeviceStateResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::setRadioCapability(int32_t p_serial, const ::aidl::android::hardware::radio::modem::RadioCapability& p_rc) {
    return edge_->request(Service::Modem, Method::ModemSetRadioCapability, p_serial,
        std::make_any<SetRadioCapabilityArguments>(p_rc),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->setRadioCapabilityResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioModem::setRadioPower(int32_t p_serial, bool p_powerOn, bool p_forEmergencyCall, bool p_preferredForEmergencyCall) {
    return edge_->request(Service::Modem, Method::ModemSetRadioPower, p_serial,
        std::make_any<SetRadioPowerArguments>(p_powerOn, p_forEmergencyCall, p_preferredForEmergencyCall),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->setRadioPowerResponse(info);
        });
}
::ndk::ScopedAStatus RadioModem::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::modem::IRadioModemResponse>& p_radioModemResponse, const std::shared_ptr<::aidl::android::hardware::radio::modem::IRadioModemIndication>& p_radioModemIndication) {
    edge_->replace(Service::Modem, p_radioModemResponse, p_radioModemIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioModem::getImei(int32_t p_serial) {
    return edge_->request(Service::Modem, Method::ModemGetImei, p_serial,
        std::make_any<GetImeiArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            return callback->getImeiResponse(info, {});
        });
}
}
