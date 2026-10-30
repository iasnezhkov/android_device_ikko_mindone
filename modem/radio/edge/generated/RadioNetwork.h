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
#include <aidl/android/hardware/radio/network/BnRadioNetwork.h>
#include <aidl/android/hardware/radio/network/CdmaRoamingType.h>
#include <aidl/android/hardware/radio/network/EmergencyMode.h>
#include <aidl/android/hardware/radio/network/EmergencyNetworkScanTrigger.h>
#include <aidl/android/hardware/radio/network/IRadioNetworkIndication.h>
#include <aidl/android/hardware/radio/network/IRadioNetworkResponse.h>
#include <aidl/android/hardware/radio/network/NetworkScanRequest.h>
#include <aidl/android/hardware/radio/network/NrDualConnectivityState.h>
#include <aidl/android/hardware/radio/network/RadioAccessSpecifier.h>
#include <aidl/android/hardware/radio/network/RadioBandMode.h>
#include <aidl/android/hardware/radio/network/SignalThresholdInfo.h>
#include <aidl/android/hardware/radio/network/UsageSetting.h>
namespace mindone::radio {
class RadioNetwork final : public ::aidl::android::hardware::radio::network::BnRadioNetwork {
public:
    explicit RadioNetwork(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {}
    ::ndk::ScopedAStatus getAllowedNetworkTypesBitmap(int32_t p_serial) override;
    using GetAllowedNetworkTypesBitmapArguments = std::tuple<>;
    ::ndk::ScopedAStatus getAvailableBandModes(int32_t p_serial) override;
    using GetAvailableBandModesArguments = std::tuple<>;
    ::ndk::ScopedAStatus getAvailableNetworks(int32_t p_serial) override;
    using GetAvailableNetworksArguments = std::tuple<>;
    ::ndk::ScopedAStatus getBarringInfo(int32_t p_serial) override;
    using GetBarringInfoArguments = std::tuple<>;
    ::ndk::ScopedAStatus getCdmaRoamingPreference(int32_t p_serial) override;
    using GetCdmaRoamingPreferenceArguments = std::tuple<>;
    ::ndk::ScopedAStatus getCellInfoList(int32_t p_serial) override;
    using GetCellInfoListArguments = std::tuple<>;
    ::ndk::ScopedAStatus getDataRegistrationState(int32_t p_serial) override;
    using GetDataRegistrationStateArguments = std::tuple<>;
    ::ndk::ScopedAStatus getImsRegistrationState(int32_t p_serial) override;
    using GetImsRegistrationStateArguments = std::tuple<>;
    ::ndk::ScopedAStatus getNetworkSelectionMode(int32_t p_serial) override;
    using GetNetworkSelectionModeArguments = std::tuple<>;
    ::ndk::ScopedAStatus getOperator(int32_t p_serial) override;
    using GetOperatorArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSignalStrength(int32_t p_serial) override;
    using GetSignalStrengthArguments = std::tuple<>;
    ::ndk::ScopedAStatus getSystemSelectionChannels(int32_t p_serial) override;
    using GetSystemSelectionChannelsArguments = std::tuple<>;
    ::ndk::ScopedAStatus getVoiceRadioTechnology(int32_t p_serial) override;
    using GetVoiceRadioTechnologyArguments = std::tuple<>;
    ::ndk::ScopedAStatus getVoiceRegistrationState(int32_t p_serial) override;
    using GetVoiceRegistrationStateArguments = std::tuple<>;
    ::ndk::ScopedAStatus isNrDualConnectivityEnabled(int32_t p_serial) override;
    using IsNrDualConnectivityEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus responseAcknowledgement() override;
    ::ndk::ScopedAStatus setAllowedNetworkTypesBitmap(int32_t p_serial, int32_t p_networkTypeBitmap) override;
    using SetAllowedNetworkTypesBitmapArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus setBandMode(int32_t p_serial, ::aidl::android::hardware::radio::network::RadioBandMode p_mode) override;
    using SetBandModeArguments = std::tuple<::aidl::android::hardware::radio::network::RadioBandMode>;
    ::ndk::ScopedAStatus setBarringPassword(int32_t p_serial, const std::string& p_facility, const std::string& p_oldPassword, const std::string& p_newPassword) override;
    using SetBarringPasswordArguments = std::tuple<std::string, std::string, std::string>;
    ::ndk::ScopedAStatus setCdmaRoamingPreference(int32_t p_serial, ::aidl::android::hardware::radio::network::CdmaRoamingType p_type) override;
    using SetCdmaRoamingPreferenceArguments = std::tuple<::aidl::android::hardware::radio::network::CdmaRoamingType>;
    ::ndk::ScopedAStatus setCellInfoListRate(int32_t p_serial, int32_t p_rate) override;
    using SetCellInfoListRateArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus setIndicationFilter(int32_t p_serial, int32_t p_indicationFilter) override;
    using SetIndicationFilterArguments = std::tuple<int32_t>;
    ::ndk::ScopedAStatus setLinkCapacityReportingCriteria(int32_t p_serial, int32_t p_hysteresisMs, int32_t p_hysteresisDlKbps, int32_t p_hysteresisUlKbps, const std::vector<int32_t>& p_thresholdsDownlinkKbps, const std::vector<int32_t>& p_thresholdsUplinkKbps, ::aidl::android::hardware::radio::AccessNetwork p_accessNetwork) override;
    using SetLinkCapacityReportingCriteriaArguments = std::tuple<int32_t, int32_t, int32_t, std::vector<int32_t>, std::vector<int32_t>, ::aidl::android::hardware::radio::AccessNetwork>;
    ::ndk::ScopedAStatus setLocationUpdates(int32_t p_serial, bool p_enable) override;
    using SetLocationUpdatesArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setNetworkSelectionModeAutomatic(int32_t p_serial) override;
    using SetNetworkSelectionModeAutomaticArguments = std::tuple<>;
    ::ndk::ScopedAStatus setNetworkSelectionModeManual(int32_t p_serial, const std::string& p_operatorNumeric, ::aidl::android::hardware::radio::AccessNetwork p_ran) override;
    using SetNetworkSelectionModeManualArguments = std::tuple<std::string, ::aidl::android::hardware::radio::AccessNetwork>;
    ::ndk::ScopedAStatus setNrDualConnectivityState(int32_t p_serial, ::aidl::android::hardware::radio::network::NrDualConnectivityState p_nrDualConnectivityState) override;
    using SetNrDualConnectivityStateArguments = std::tuple<::aidl::android::hardware::radio::network::NrDualConnectivityState>;
    ::ndk::ScopedAStatus setResponseFunctions(const std::shared_ptr<::aidl::android::hardware::radio::network::IRadioNetworkResponse>& p_radioNetworkResponse, const std::shared_ptr<::aidl::android::hardware::radio::network::IRadioNetworkIndication>& p_radioNetworkIndication) override;
    ::ndk::ScopedAStatus setSignalStrengthReportingCriteria(int32_t p_serial, const std::vector<::aidl::android::hardware::radio::network::SignalThresholdInfo>& p_signalThresholdInfos) override;
    using SetSignalStrengthReportingCriteriaArguments = std::tuple<std::vector<::aidl::android::hardware::radio::network::SignalThresholdInfo>>;
    ::ndk::ScopedAStatus setSuppServiceNotifications(int32_t p_serial, bool p_enable) override;
    using SetSuppServiceNotificationsArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setSystemSelectionChannels(int32_t p_serial, bool p_specifyChannels, const std::vector<::aidl::android::hardware::radio::network::RadioAccessSpecifier>& p_specifiers) override;
    using SetSystemSelectionChannelsArguments = std::tuple<bool, std::vector<::aidl::android::hardware::radio::network::RadioAccessSpecifier>>;
    ::ndk::ScopedAStatus startNetworkScan(int32_t p_serial, const ::aidl::android::hardware::radio::network::NetworkScanRequest& p_request) override;
    using StartNetworkScanArguments = std::tuple<::aidl::android::hardware::radio::network::NetworkScanRequest>;
    ::ndk::ScopedAStatus stopNetworkScan(int32_t p_serial) override;
    using StopNetworkScanArguments = std::tuple<>;
    ::ndk::ScopedAStatus supplyNetworkDepersonalization(int32_t p_serial, const std::string& p_netPin) override;
    using SupplyNetworkDepersonalizationArguments = std::tuple<std::string>;
    ::ndk::ScopedAStatus setUsageSetting(int32_t p_serial, ::aidl::android::hardware::radio::network::UsageSetting p_usageSetting) override;
    using SetUsageSettingArguments = std::tuple<::aidl::android::hardware::radio::network::UsageSetting>;
    ::ndk::ScopedAStatus getUsageSetting(int32_t p_serial) override;
    using GetUsageSettingArguments = std::tuple<>;
    ::ndk::ScopedAStatus setEmergencyMode(int32_t p_serial, ::aidl::android::hardware::radio::network::EmergencyMode p_emcModeType) override;
    using SetEmergencyModeArguments = std::tuple<::aidl::android::hardware::radio::network::EmergencyMode>;
    ::ndk::ScopedAStatus triggerEmergencyNetworkScan(int32_t p_serial, const ::aidl::android::hardware::radio::network::EmergencyNetworkScanTrigger& p_request) override;
    using TriggerEmergencyNetworkScanArguments = std::tuple<::aidl::android::hardware::radio::network::EmergencyNetworkScanTrigger>;
    ::ndk::ScopedAStatus cancelEmergencyNetworkScan(int32_t p_serial, bool p_resetScan) override;
    using CancelEmergencyNetworkScanArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus exitEmergencyMode(int32_t p_serial) override;
    using ExitEmergencyModeArguments = std::tuple<>;
    ::ndk::ScopedAStatus setNullCipherAndIntegrityEnabled(int32_t p_serial, bool p_enabled) override;
    using SetNullCipherAndIntegrityEnabledArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus isNullCipherAndIntegrityEnabled(int32_t p_serial) override;
    using IsNullCipherAndIntegrityEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus isN1ModeEnabled(int32_t p_serial) override;
    using IsN1ModeEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus setN1ModeEnabled(int32_t p_serial, bool p_enable) override;
    using SetN1ModeEnabledArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus isCellularIdentifierTransparencyEnabled(int32_t p_serial) override;
    using IsCellularIdentifierTransparencyEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus setCellularIdentifierTransparencyEnabled(int32_t p_serial, bool p_enabled) override;
    using SetCellularIdentifierTransparencyEnabledArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus setSecurityAlgorithmsUpdatedEnabled(int32_t p_serial, bool p_enable) override;
    using SetSecurityAlgorithmsUpdatedEnabledArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus isSecurityAlgorithmsUpdatedEnabled(int32_t p_serial) override;
    using IsSecurityAlgorithmsUpdatedEnabledArguments = std::tuple<>;
    ::ndk::ScopedAStatus setSatellitePlmn(int32_t p_serial, const std::vector<std::string>& p_carrierPlmnArray, const std::vector<std::string>& p_allSatellitePlmnArray) override;
    using SetSatellitePlmnArguments = std::tuple<std::vector<std::string>, std::vector<std::string>>;
    ::ndk::ScopedAStatus setSatelliteEnabledForCarrier(int32_t p_serial, bool p_satelliteEnabled) override;
    using SetSatelliteEnabledForCarrierArguments = std::tuple<bool>;
    ::ndk::ScopedAStatus isSatelliteEnabledForCarrier(int32_t p_serial) override;
    using IsSatelliteEnabledForCarrierArguments = std::tuple<>;
private:
    std::shared_ptr<Edge> edge_;
};
}
