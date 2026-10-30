/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

Routes makeRoutes(uint32_t uiccQueryKind) {
    CHECK(uiccQueryKind == MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED || uiccQueryKind == MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT);
    Routes routes;
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetVoiceRegistrationState), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetVoiceRegistrationStateArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_VOICE_REGISTRATION_STATE;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::network::RegStateResult out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_REGISTRATION || !(convert::registration(value->registration, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getVoiceRegistrationStateResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetDataRegistrationState), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetDataRegistrationStateArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_DATA_REGISTRATION_STATE;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::network::RegStateResult out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_REGISTRATION || !(convert::registration(value->registration, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getDataRegistrationStateResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetOperator), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetOperatorArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_OPERATOR;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            std::string out0{};
            std::string out1{};
            std::string out2{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_OPERATOR || !(convert::text(value->operator_info.alpha_long, out0) && convert::text(value->operator_info.alpha_short, out1) && convert::text(value->operator_info.numeric, out2))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getOperatorResponse(info, out0, out1, out2);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetNetworkSelectionMode), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetNetworkSelectionModeArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_NETWORK_SELECTION_MODE;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            bool out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_SELECTION || !(convert::selection(value->selection, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getNetworkSelectionModeResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetSignalStrength), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetSignalStrengthArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_SIGNAL_STRENGTH;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::network::SignalStrength out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_SIGNAL || !(convert::signal(value->signal, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getSignalStrengthResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetCellInfoList), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetCellInfoListArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_CELL_INFO_LIST;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            std::vector<::aidl::android::hardware::radio::network::CellInfo> out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_CELLS || !(convert::cells(*value, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getCellInfoListResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetImsRegistrationState), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetImsRegistrationStateArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_GET_IMS_REGISTRATION_STATE;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            bool out0{};
            ::aidl::android::hardware::radio::RadioTechnologyFamily out1{};
            if (info.error == RadioError::NONE) {
                static_cast<void>(value);
                info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getImsRegistrationStateResponse(info, out0, out1);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkSetIndicationFilter), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::SetIndicationFilterArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_SET_INDICATION_FILTER;
            storage.request.mask = static_cast<uint32_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setIndicationFilterResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkSetSignalStrengthReportingCriteria), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::SetSignalStrengthReportingCriteriaArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA;
            if (!encodeSignalCriteria(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setSignalStrengthReportingCriteriaResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkSetLinkCapacityReportingCriteria), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::SetLinkCapacityReportingCriteriaArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_NETWORK_SET_LINK_CAPACITY_REPORTING_CRITERIA;
            if (!encodeLinkCriteria(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_NETWORK_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setLinkCapacityReportingCriteriaResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemGetBasebandVersion), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::GetBasebandVersionArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_BASEBAND_VERSION;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            std::string out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_BASEBAND || !(convert::text(value->text, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getBasebandVersionResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemGetImei), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::GetImeiArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_IMEI;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            std::optional<::aidl::android::hardware::radio::modem::ImeiInfo> out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_IMEI || !(convert::imei(value->imei, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getImeiResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemGetDeviceIdentity), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::GetDeviceIdentityArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_DEVICE_IDENTITY;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            std::string out0{};
            std::string out1{};
            std::string out2{};
            std::string out3{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_IDENTITY || !(convert::text(value->identity.imei, out0) && convert::text(value->identity.imeisv, out1) && convert::text(value->identity.esn, out2) && convert::text(value->identity.meid, out3))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getDeviceIdentityResponse(info, out0, out1, out2, out3);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemGetModemActivityInfo), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::GetModemActivityInfoArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_MODEM_ACTIVITY_INFO;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::modem::ActivityStatsInfo out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_ACTIVITY || !(convert::activity(value->activity, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getModemActivityInfoResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemGetModemStackStatus), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::GetModemStackStatusArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_MODEM_STACK_STATUS;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            bool out0{};
            if (info.error == RadioError::NONE) {
                static_cast<void>(value);
                info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getModemStackStatusResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemGetRadioCapability), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::GetRadioCapabilityArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_RADIO_CAPABILITY;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::modem::RadioCapability out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_CAPABILITY || !(convert::capability(value->capability, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getRadioCapabilityResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemSetRadioPower), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::SetRadioPowerArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_RADIO_POWER;
            if (!encodePower(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setRadioPowerResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemEnableModem), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::EnableModemArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_ENABLE_MODEM;
            storage.request.boolean = static_cast<uint8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->enableModemResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Modem, Method::ModemSendDeviceState), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioModem::SendDeviceStateArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SEND_DEVICE_STATE;
            if (!encodeDeviceState(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::modem::IRadioModemResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->sendDeviceStateResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Sim, Method::SimGetIccCardStatus), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioSim::GetIccCardStatusArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_ICC_CARD_STATUS;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::sim::CardStatus out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_CARD || !(convert::card(value->card, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getIccCardStatusResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Sim, Method::SimAreUiccApplicationsEnabled), Route{
        [uiccQueryKind](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioSim::AreUiccApplicationsEnabledArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = uiccQueryKind;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            ResponseInfo info = original;
            bool out0{};
            if (info.error == RadioError::NONE) {
                static_cast<void>(value);
                info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->areUiccApplicationsEnabledResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Sim, Method::SimEnableUiccApplications), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioSim::EnableUiccApplicationsArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_ENABLE_UICC_APPLICATIONS;
            storage.request.boolean = static_cast<uint8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->enableUiccApplicationsResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Sim, Method::SimSetCarrierInfoForImsiEncryption), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioSim::SetCarrierInfoForImsiEncryptionArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_CARRIER_INFO_FOR_IMSI_ENCRYPTION;
            if (!encodeImsiEncryption(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::sim::IRadioSimResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setCarrierInfoForImsiEncryptionResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Config, Method::ConfigGetHalDeviceCapabilities), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioConfig::GetHalDeviceCapabilitiesArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_HAL_DEVICE_CAPABILITIES;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            ResponseInfo info = original;
            bool out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_REDUCED_FEATURE_SET1 || !(convert::boolean(value->boolean, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getHalDeviceCapabilitiesResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Config, Method::ConfigGetSimSlotsStatus), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioConfig::GetSimSlotsStatusArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_SIM_SLOTS_STATUS;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            ResponseInfo info = original;
            std::vector<::aidl::android::hardware::radio::config::SimSlotStatus> out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_SLOTS || !(convert::slots(*value, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getSimSlotsStatusResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Config, Method::ConfigGetPhoneCapability), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioConfig::GetPhoneCapabilityArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_PHONE_CAPABILITY;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::config::PhoneCapability out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_PHONE || !(convert::phone(value->phone, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getPhoneCapabilityResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Config, Method::ConfigGetNumOfLiveModems), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioConfig::GetNumOfLiveModemsArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_NUM_OF_LIVE_MODEMS;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            ResponseInfo info = original;
            int8_t out0{};
            if (info.error == RadioError::NONE) {
                static_cast<void>(value);
                info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getNumOfLiveModemsResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Config, Method::ConfigSetNumOfLiveModems), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioConfig::SetNumOfLiveModemsArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_NUM_OF_LIVE_MODEMS;
            if (!validation::positive(static_cast<int32_t>(std::get<0>(*args)))) return MR_INVALID_ARGUMENT;
            storage.request.small_integer = static_cast<int8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setNumOfLiveModemsResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Config, Method::ConfigSetPreferredDataModem), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioConfig::SetPreferredDataModemArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_PREFERRED_DATA_MODEM;
            if (!validation::nonnegative(static_cast<int32_t>(std::get<0>(*args)))) return MR_INVALID_ARGUMENT;
            storage.request.small_integer = static_cast<int8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::config::IRadioConfigResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setPreferredDataModemResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Voice, Method::VoiceGetCurrentCalls), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioVoice::GetCurrentCallsArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_CURRENT_CALLS;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            ResponseInfo info = original;
            std::vector<::aidl::android::hardware::radio::voice::Call> out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_CURRENT_CALLS || !(convert::calls(*value, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getCurrentCallsResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Voice, Method::VoiceSetTtyMode), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioVoice::SetTtyModeArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_TTY_MODE;
            if (!validation::tty(static_cast<int32_t>(std::get<0>(*args)))) return MR_INVALID_ARGUMENT;
            storage.request.integer = static_cast<int32_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::voice::IRadioVoiceResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setTtyModeResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Messaging, Method::MessagingSetGsmBroadcastConfig), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioMessaging::SetGsmBroadcastConfigArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_GSM_BROADCAST_CONFIG;
            if (!encodeBroadcast(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::messaging::IRadioMessagingResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setGsmBroadcastConfigResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Data, Method::DataSetDataProfile), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioData::SetDataProfileArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_DATA_PROFILE;
            if (!encodeDataProfiles(*args, storage)) return MR_INVALID_ARGUMENT;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::data::IRadioDataResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setDataProfileResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkSetNullCipherAndIntegrityEnabled), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::SetNullCipherAndIntegrityEnabledArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_NULL_CIPHER_AND_INTEGRITY_ENABLED;
            storage.request.boolean = static_cast<uint8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setNullCipherAndIntegrityEnabledResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkSetSecurityAlgorithmsUpdatedEnabled), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::SetSecurityAlgorithmsUpdatedEnabledArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_SECURITY_ALGORITHMS_UPDATED_ENABLED;
            storage.request.boolean = static_cast<uint8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setSecurityAlgorithmsUpdatedEnabledResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkSetCellularIdentifierTransparencyEnabled), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::SetCellularIdentifierTransparencyEnabledArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_SET_CELLULAR_IDENTIFIER_DISCLOSED_ENABLED;
            storage.request.boolean = static_cast<uint8_t>(std::get<0>(*args));
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOID || !(true)) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->setCellularIdentifierTransparencyEnabledResponse(info);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkIsSatelliteEnabledForCarrier), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::IsSatelliteEnabledForCarrierArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_IS_SATELLITE_ENABLED_FOR_CARRIER;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            bool out0{};
            if (info.error == RadioError::NONE) {
                static_cast<void>(value);
                info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->isSatelliteEnabledForCarrierResponse(info, out0);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetBarringInfo), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetBarringInfoArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_BARRING_INFO;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::network::CellIdentity out0{};
            std::vector<::aidl::android::hardware::radio::network::BarringInfo> out1{};
            if (info.error == RadioError::NONE) {
                static_cast<void>(value);
                info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getBarringInfoResponse(info, out0, out1);
        }
    });
    routes.emplace(std::make_pair(Service::Network, Method::NetworkGetVoiceRadioTechnology), Route{
        [](mr_runtime runtime, uint64_t id, const std::any& payload) {
            const auto* args = std::any_cast<RadioNetwork::GetVoiceRadioTechnologyArguments>(&payload);
            if (!args) return MR_INVALID_ARGUMENT;
            RequestStorage storage;
            storage.request.kind = MR_REQUEST_MODEM_GET_VOICE_RADIO_TECHNOLOGY;
            return mr_submit(runtime, id, &storage.request);
        },
        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {
            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::network::IRadioNetworkResponse>(snapshot.response);
            ResponseInfo info = original;
            ::aidl::android::hardware::radio::RadioTechnology out0{};
            if (info.error == RadioError::NONE) {
                if (!value || value->kind != MR_VALUE_MODEM_VOICE_RADIO_TECHNOLOGY || !(convert::technology(value->integer, out0))) info.error = RadioError::INVALID_RESPONSE;
            }
            return callback->getVoiceRadioTechnologyResponse(info, out0);
        }
    });
    return routes;
}
