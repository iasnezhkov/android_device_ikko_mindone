/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "RadioNetwork.h"
namespace mindone::radio {
::ndk::ScopedAStatus RadioNetwork::getAllowedNetworkTypesBitmap(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetAllowedNetworkTypesBitmap, p_serial,
        std::make_any<GetAllowedNetworkTypesBitmapArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getAllowedNetworkTypesBitmapResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getAvailableBandModes(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetAvailableBandModes, p_serial,
        std::make_any<GetAvailableBandModesArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getAvailableBandModesResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getAvailableNetworks(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetAvailableNetworks, p_serial,
        std::make_any<GetAvailableNetworksArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getAvailableNetworksResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getBarringInfo(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetBarringInfo, p_serial,
        std::make_any<GetBarringInfoArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getBarringInfoResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getCdmaRoamingPreference(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetCdmaRoamingPreference, p_serial,
        std::make_any<GetCdmaRoamingPreferenceArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getCdmaRoamingPreferenceResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getCellInfoList(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetCellInfoList, p_serial,
        std::make_any<GetCellInfoListArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getCellInfoListResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getDataRegistrationState(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetDataRegistrationState, p_serial,
        std::make_any<GetDataRegistrationStateArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getDataRegistrationStateResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getImsRegistrationState(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetImsRegistrationState, p_serial,
        std::make_any<GetImsRegistrationStateArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getImsRegistrationStateResponse(info, {}, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getNetworkSelectionMode(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetNetworkSelectionMode, p_serial,
        std::make_any<GetNetworkSelectionModeArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getNetworkSelectionModeResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getOperator(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetOperator, p_serial,
        std::make_any<GetOperatorArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getOperatorResponse(info, {}, {}, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getSignalStrength(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetSignalStrength, p_serial,
        std::make_any<GetSignalStrengthArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getSignalStrengthResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getSystemSelectionChannels(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetSystemSelectionChannels, p_serial,
        std::make_any<GetSystemSelectionChannelsArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getSystemSelectionChannelsResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getVoiceRadioTechnology(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetVoiceRadioTechnology, p_serial,
        std::make_any<GetVoiceRadioTechnologyArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getVoiceRadioTechnologyResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::getVoiceRegistrationState(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetVoiceRegistrationState, p_serial,
        std::make_any<GetVoiceRegistrationStateArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getVoiceRegistrationStateResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::isNrDualConnectivityEnabled(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkIsNrDualConnectivityEnabled, p_serial,
        std::make_any<IsNrDualConnectivityEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->isNrDualConnectivityEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::responseAcknowledgement() {
    edge_->acknowledge(Service::Network);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioNetwork::setAllowedNetworkTypesBitmap(int32_t p_serial, int32_t p_networkTypeBitmap) {
    return edge_->request(Service::Network, Method::NetworkSetAllowedNetworkTypesBitmap, p_serial,
        std::make_any<SetAllowedNetworkTypesBitmapArguments>(p_networkTypeBitmap),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setAllowedNetworkTypesBitmapResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setBandMode(int32_t p_serial, ::aidl::android::hardware::radio::network::RadioBandMode p_mode) {
    return edge_->request(Service::Network, Method::NetworkSetBandMode, p_serial,
        std::make_any<SetBandModeArguments>(p_mode),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setBandModeResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setBarringPassword(int32_t p_serial, const std::string& p_facility, const std::string& p_oldPassword, const std::string& p_newPassword) {
    return edge_->request(Service::Network, Method::NetworkSetBarringPassword, p_serial,
        std::make_any<SetBarringPasswordArguments>(p_facility, p_oldPassword, p_newPassword),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setBarringPasswordResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setCdmaRoamingPreference(int32_t p_serial, ::aidl::android::hardware::radio::network::CdmaRoamingType p_type) {
    return edge_->request(Service::Network, Method::NetworkSetCdmaRoamingPreference, p_serial,
        std::make_any<SetCdmaRoamingPreferenceArguments>(p_type),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setCdmaRoamingPreferenceResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setCellInfoListRate(int32_t p_serial, int32_t p_rate) {
    return edge_->request(Service::Network, Method::NetworkSetCellInfoListRate, p_serial,
        std::make_any<SetCellInfoListRateArguments>(p_rate),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setCellInfoListRateResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setIndicationFilter(int32_t p_serial, int32_t p_indicationFilter) {
    return edge_->request(Service::Network, Method::NetworkSetIndicationFilter, p_serial,
        std::make_any<SetIndicationFilterArguments>(p_indicationFilter),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setIndicationFilterResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setLinkCapacityReportingCriteria(int32_t p_serial, int32_t p_hysteresisMs, int32_t p_hysteresisDlKbps, int32_t p_hysteresisUlKbps, const std::vector<int32_t>& p_thresholdsDownlinkKbps, const std::vector<int32_t>& p_thresholdsUplinkKbps, ::aidl::android::hardware::radio::AccessNetwork p_accessNetwork) {
    return edge_->request(Service::Network, Method::NetworkSetLinkCapacityReportingCriteria, p_serial,
        std::make_any<SetLinkCapacityReportingCriteriaArguments>(p_hysteresisMs, p_hysteresisDlKbps, p_hysteresisUlKbps, p_thresholdsDownlinkKbps, p_thresholdsUplinkKbps, p_accessNetwork),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setLinkCapacityReportingCriteriaResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setLocationUpdates(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Network, Method::NetworkSetLocationUpdates, p_serial,
        std::make_any<SetLocationUpdatesArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setLocationUpdatesResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setNetworkSelectionModeAutomatic(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkSetNetworkSelectionModeAutomatic, p_serial,
        std::make_any<SetNetworkSelectionModeAutomaticArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setNetworkSelectionModeAutomaticResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setNetworkSelectionModeManual(int32_t p_serial, const std::string& p_operatorNumeric, ::aidl::android::hardware::radio::AccessNetwork p_ran) {
    return edge_->request(Service::Network, Method::NetworkSetNetworkSelectionModeManual, p_serial,
        std::make_any<SetNetworkSelectionModeManualArguments>(p_operatorNumeric, p_ran),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setNetworkSelectionModeManualResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setNrDualConnectivityState(int32_t p_serial, ::aidl::android::hardware::radio::network::NrDualConnectivityState p_nrDualConnectivityState) {
    return edge_->request(Service::Network, Method::NetworkSetNrDualConnectivityState, p_serial,
        std::make_any<SetNrDualConnectivityStateArguments>(p_nrDualConnectivityState),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setNrDualConnectivityStateResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::network::IRadioNetworkResponse>& p_radioNetworkResponse, const std::shared_ptr<::aidl::android::hardware::radio::network::IRadioNetworkIndication>& p_radioNetworkIndication) {
    edge_->replace(Service::Network, p_radioNetworkResponse, p_radioNetworkIndication);
    return ::ndk::ScopedAStatus::ok();
}
::ndk::ScopedAStatus RadioNetwork::setSignalStrengthReportingCriteria(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::network::SignalThresholdInfo>& p_signalThresholdInfos) {
    return edge_->request(Service::Network, Method::NetworkSetSignalStrengthReportingCriteria, p_serial,
        std::make_any<SetSignalStrengthReportingCriteriaArguments>(p_signalThresholdInfos),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setSignalStrengthReportingCriteriaResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setSuppServiceNotifications(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Network, Method::NetworkSetSuppServiceNotifications, p_serial,
        std::make_any<SetSuppServiceNotificationsArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setSuppServiceNotificationsResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setSystemSelectionChannels(int32_t p_serial, bool p_specifyChannels, const std::vector<::aidl::android::hardware::radio::network::RadioAccessSpecifier>& p_specifiers) {
    return edge_->request(Service::Network, Method::NetworkSetSystemSelectionChannels, p_serial,
        std::make_any<SetSystemSelectionChannelsArguments>(p_specifyChannels, p_specifiers),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setSystemSelectionChannelsResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::startNetworkScan(int32_t p_serial, const ::aidl::android::hardware::radio::network::NetworkScanRequest& p_request) {
    return edge_->request(Service::Network, Method::NetworkStartNetworkScan, p_serial,
        std::make_any<StartNetworkScanArguments>(p_request),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->startNetworkScanResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::stopNetworkScan(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkStopNetworkScan, p_serial,
        std::make_any<StopNetworkScanArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->stopNetworkScanResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::supplyNetworkDepersonalization(int32_t p_serial, const std::string& p_netPin) {
    return edge_->request(Service::Network, Method::NetworkSupplyNetworkDepersonalization, p_serial,
        std::make_any<SupplyNetworkDepersonalizationArguments>(p_netPin),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->supplyNetworkDepersonalizationResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::setUsageSetting(int32_t p_serial, ::aidl::android::hardware::radio::network::UsageSetting p_usageSetting) {
    return edge_->request(Service::Network, Method::NetworkSetUsageSetting, p_serial,
        std::make_any<SetUsageSettingArguments>(p_usageSetting),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setUsageSettingResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::getUsageSetting(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkGetUsageSetting, p_serial,
        std::make_any<GetUsageSettingArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->getUsageSettingResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::setEmergencyMode(int32_t p_serial, ::aidl::android::hardware::radio::network::EmergencyMode p_emcModeType) {
    return edge_->request(Service::Network, Method::NetworkSetEmergencyMode, p_serial,
        std::make_any<SetEmergencyModeArguments>(p_emcModeType),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setEmergencyModeResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::triggerEmergencyNetworkScan(int32_t p_serial, const ::aidl::android::hardware::radio::network::EmergencyNetworkScanTrigger& p_request) {
    return edge_->request(Service::Network, Method::NetworkTriggerEmergencyNetworkScan, p_serial,
        std::make_any<TriggerEmergencyNetworkScanArguments>(p_request),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->triggerEmergencyNetworkScanResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::cancelEmergencyNetworkScan(int32_t p_serial, bool p_resetScan) {
    return edge_->request(Service::Network, Method::NetworkCancelEmergencyNetworkScan, p_serial,
        std::make_any<CancelEmergencyNetworkScanArguments>(p_resetScan),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->cancelEmergencyNetworkScanResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::exitEmergencyMode(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkExitEmergencyMode, p_serial,
        std::make_any<ExitEmergencyModeArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->exitEmergencyModeResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setNullCipherAndIntegrityEnabled(int32_t p_serial, bool p_enabled) {
    return edge_->request(Service::Network, Method::NetworkSetNullCipherAndIntegrityEnabled, p_serial,
        std::make_any<SetNullCipherAndIntegrityEnabledArguments>(p_enabled),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setNullCipherAndIntegrityEnabledResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::isNullCipherAndIntegrityEnabled(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkIsNullCipherAndIntegrityEnabled, p_serial,
        std::make_any<IsNullCipherAndIntegrityEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->isNullCipherAndIntegrityEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::isN1ModeEnabled(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkIsN1ModeEnabled, p_serial,
        std::make_any<IsN1ModeEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->isN1ModeEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::setN1ModeEnabled(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Network, Method::NetworkSetN1ModeEnabled, p_serial,
        std::make_any<SetN1ModeEnabledArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setN1ModeEnabledResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::isCellularIdentifierTransparencyEnabled(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkIsCellularIdentifierTransparencyEnabled, p_serial,
        std::make_any<IsCellularIdentifierTransparencyEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->isCellularIdentifierTransparencyEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::setCellularIdentifierTransparencyEnabled(int32_t p_serial, bool p_enabled) {
    return edge_->request(Service::Network, Method::NetworkSetCellularIdentifierTransparencyEnabled, p_serial,
        std::make_any<SetCellularIdentifierTransparencyEnabledArguments>(p_enabled),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setCellularIdentifierTransparencyEnabledResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setSecurityAlgorithmsUpdatedEnabled(int32_t p_serial, bool p_enable) {
    return edge_->request(Service::Network, Method::NetworkSetSecurityAlgorithmsUpdatedEnabled, p_serial,
        std::make_any<SetSecurityAlgorithmsUpdatedEnabledArguments>(p_enable),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setSecurityAlgorithmsUpdatedEnabledResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::isSecurityAlgorithmsUpdatedEnabled(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkIsSecurityAlgorithmsUpdatedEnabled, p_serial,
        std::make_any<IsSecurityAlgorithmsUpdatedEnabledArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->isSecurityAlgorithmsUpdatedEnabledResponse(info, {});
        });
}
::ndk::ScopedAStatus RadioNetwork::setSatellitePlmn(int32_t p_serial, const std::vector<std::string>& p_carrierPlmnArray, const std::vector<std::string>& p_allSatellitePlmnArray) {
    return edge_->request(Service::Network, Method::NetworkSetSatellitePlmn, p_serial,
        std::make_any<SetSatellitePlmnArguments>(p_carrierPlmnArray, p_allSatellitePlmnArray),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setSatellitePlmnResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::setSatelliteEnabledForCarrier(int32_t p_serial, bool p_satelliteEnabled) {
    return edge_->request(Service::Network, Method::NetworkSetSatelliteEnabledForCarrier, p_serial,
        std::make_any<SetSatelliteEnabledForCarrierArguments>(p_satelliteEnabled),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->setSatelliteEnabledForCarrierResponse(info);
        });
}
::ndk::ScopedAStatus RadioNetwork::isSatelliteEnabledForCarrier(int32_t p_serial) {
    return edge_->request(Service::Network, Method::NetworkIsSatelliteEnabledForCarrier, p_serial,
        std::make_any<IsSatelliteEnabledForCarrierArguments>(),
        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            return callback->isSatelliteEnabledForCarrierResponse(info, {});
        });
}
}
