/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum RadioError {
    None = 0, RadioNotAvailable = 1, GenericFailure = 2, RequestNotSupported = 6,
    InternalErr = 38, ModemErr = 40, SimErr = 43, InvalidArguments = 44,
    SimBusy = 51, OperationNotAllowed = 54, EmptyRecord = 55, InvalidResponse = 66, OemError1 = 501,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum DeviceStateType { PowerSaveMode = 0, ChargingState = 1, LowDataExpected = 2 }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum ImeiType { Invalid = 0, Primary = 1, Secondary = 2 }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ImeiInfo { pub kind: ImeiType, pub imei: String, pub svn: String }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeviceIdentity { pub imei: String, pub imeisv: String, pub esn: String, pub meid: String }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActivityStatsInfo {
    pub sleep_mode_time_ms: i32, pub idle_mode_time_ms: i32,
    pub tech_specific_info: Vec<ActivityStatsTechSpecificInfo>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActivityStatsTechSpecificInfo {
    pub rat: i32, pub frequency_range: i32, pub txm_mode_time_ms: [i32; 5], pub rx_mode_time_ms: i32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RadioCapability {
    pub session: i32, pub phase: i32, pub raf: i32, pub logical_modem_uuid: String, pub status: i32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum CardState { Absent = 0, Present = 1, Error = 2, Restricted = 3 }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SlotPortMapping { pub physical_slot_id: i32, pub port_id: i32 }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CardStatus {
    pub card_state: CardState, pub universal_pin_state: i32,
    pub gsm_umts_subscription_app_index: i32, pub cdma_subscription_app_index: i32,
    pub ims_subscription_app_index: i32, pub applications: Vec<AppStatus>,
    pub atr: String, pub iccid: String, pub eid: String, pub slot_map: SlotPortMapping,
    pub supported_mep_mode: i32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AppStatus {
    pub app_type: i32, pub app_state: i32, pub perso_substate: i32, pub aid_ptr: String,
    pub app_label_ptr: String, pub pin1_replaced: bool, pub pin1: i32, pub pin2: i32,
}

impl CardStatus {
    pub fn absent() -> Self {
        Self { card_state: CardState::Absent, universal_pin_state: 0,
            gsm_umts_subscription_app_index: -1, cdma_subscription_app_index: -1,
            ims_subscription_app_index: -1, applications: Vec::new(), atr: String::new(),
            iccid: String::new(), eid: String::new(),
            slot_map: SlotPortMapping { physical_slot_id: 0, port_id: 0 }, supported_mep_mode: 0 }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SimPortInfo { pub icc_id: String, pub logical_slot_id: i32, pub port_active: bool }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SimSlotStatus {
    pub card_state: CardState, pub atr: String, pub eid: String,
    pub port_info: Vec<SimPortInfo>, pub supported_mep_mode: i32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PhoneCapability {
    pub max_active_data: i8, pub max_active_internet_data: i8,
    pub is_internet_lingering_supported: bool, pub logical_modem_ids: Vec<i8>, pub max_active_voice: i8,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GsmBroadcastSmsConfigInfo {
    pub from_service_id: i32, pub to_service_id: i32, pub from_code_scheme: i32,
    pub to_code_scheme: i32, pub selected: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Request {
    SetRadioPower { on: bool, for_emergency_call: bool, preferred_for_emergency_call: bool },
    GetBasebandVersion, GetImei, GetDeviceIdentity, GetModemActivityInfo, GetModemStackStatus,
    EnableModem(bool), SendDeviceState(DeviceStateType, bool), GetRadioCapability,
    GetIccCardStatus, GetUiccApplicationsEnablement, AreUiccApplicationsEnabled,
    EnableUiccApplications(bool), SetCarrierInfoForImsiEncryption,
    GetHalDeviceCapabilities, GetSimSlotsStatus, GetPhoneCapability,
    GetNumOfLiveModems, SetNumOfLiveModems(i8), SetPreferredDataModem(i8),
    GetCurrentCalls, SetTtyMode(i32), SetGsmBroadcastConfig(Vec<GsmBroadcastSmsConfigInfo>),
    SetDataProfile { profile_count: usize }, SetNullCipherAndIntegrityEnabled(bool), SetSecurityAlgorithmsUpdatedEnabled(bool),
    SetCellularIdentifierDisclosedEnabled(bool), IsSatelliteEnabledForCarrier, GetBarringInfo,
    GetVoiceRadioTechnology,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Value {
    Void, Baseband(String), Imei(ImeiInfo), Identity(DeviceIdentity), Activity(ActivityStatsInfo),
    Capability(RadioCapability), Card(CardStatus), Slots(Vec<SimSlotStatus>), Phone(PhoneCapability),
    ReducedFeatureSet1(bool), CurrentCalls(Vec<Call>), VoiceRadioTechnology(i32),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Call {
    pub state: i32, pub index: i32, pub toa: i32, pub is_mpty: bool, pub is_mt: bool,
    pub als: i8, pub is_voice: bool, pub is_voice_privacy: bool, pub number: String,
    pub number_presentation: i32, pub name: String, pub name_presentation: i32,
    pub uus_info: Vec<UusInfo>, pub audio_quality: i32, pub forwarded_number: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Response { pub serial: i32, pub error: RadioError, pub value: Option<Value> }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UusInfo { pub uus_type: i32, pub uus_dcs: i32, pub uus_data: String }
