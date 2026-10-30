/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum RadioError {
    None = 0, RadioNotAvailable = 1, GenericFailure = 2, RequestNotSupported = 6,
    InternalErr = 38, ModemErr = 40, InvalidArguments = 44, InvalidResponse = 66,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum RegState { NotRegistered = 0, Home = 1, Searching = 2, Denied = 3, Unknown = 4, Roaming = 5 }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum Rat { Unknown = 0, Gsm = 16, Umts = 3, Lte = 14 }

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CellIdentity {
    Gsm { lac: u32, cid: u32, plmn: String },
    Wcdma { lac: u32, cid: u32, plmn: String },
    Lte { tac: u32, ci: u32, plmn: String },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RegStateResult {
    pub reg_state: RegState, pub rat: Rat, pub cell_identity: Option<CellIdentity>,
    pub reason_for_denial: i32, pub registered_plmn: String,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct OperatorInfo { pub alpha_long: String, pub alpha_short: String, pub numeric: String }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SelectionMode { Automatic, Manual, DeregisteredUnverified, ManualAutomaticUnverified }

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct SignalStrength {
    pub gsm: Option<GsmSignal>, pub lte: Option<LteSignal>, pub wcdma: Option<WcdmaSignal>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GsmSignal { pub signal_strength: Option<u8>, pub bit_error_rate: Option<u8> }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LteSignal { pub rsrp: Option<i32>, pub rsrq: Option<i32>, pub rssnr: Option<i32> }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WcdmaSignal { pub signal_strength: Option<u8>, pub rscp: Option<i32>, pub ecno: Option<i32> }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CellInfo { pub registered: bool, pub identity: CellIdentity, pub signal: SignalStrength }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SignalCriteria {
    pub access_network: i32, pub measurement: i32, pub enabled: bool,
    pub hysteresis_ms: i32, pub hysteresis_db: i32, pub thresholds: Vec<i32>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LinkCriteria {
    pub access_network: i32, pub hysteresis_ms: i32, pub hysteresis_dl_kbps: i32,
    pub hysteresis_ul_kbps: i32, pub thresholds_dl_kbps: Vec<i32>, pub thresholds_ul_kbps: Vec<i32>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Request {
    GetVoiceRegistrationState, GetDataRegistrationState, GetOperator, GetNetworkSelectionMode,
    GetSignalStrength, GetCellInfoList, GetImsRegistrationState, SetIndicationFilter(u32),
    SetSignalStrengthReportingCriteria(Vec<SignalCriteria>), SetLinkCapacityReportingCriteria(LinkCriteria),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Value {
    Void, Registration(RegStateResult), Operator(OperatorInfo), Selection(SelectionMode),
    Signal(SignalStrength), Cells(Vec<CellInfo>),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Response { pub serial: i32, pub error: RadioError, pub value: Option<Value> }
