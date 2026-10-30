/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{arena::Arena, wire::*};
use mindone_radio_modem::types as m;
use mindone_radio_network::types as n;
use mindone_radio_runtime::{
    core::{Indication, Request, Value},
    Delivery,
};
use mindone_radio_state::model as s;

pub(crate) type Result<T> = std::result::Result<T, i32>;
pub(crate) trait Reader {
    fn array<T: Copy>(&self, data: *const T, len: u32) -> Result<Vec<T>>;
}
pub(crate) trait Codec: Sized {
    type Wire: Copy + Default;
    fn encode(&self, arena: &mut Arena) -> Self::Wire;
    fn decode(wire: Self::Wire, reader: &impl Reader) -> Result<Self>;
}
impl Codec for u8 {
    type Wire = u8;
    fn encode(&self, _: &mut Arena) -> u8 {
        *self
    }
    fn decode(wire: u8, _: &impl Reader) -> Result<Self> {
        Ok(wire)
    }
}
impl Codec for i8 {
    type Wire = i8;
    fn encode(&self, _: &mut Arena) -> i8 {
        *self
    }
    fn decode(wire: i8, _: &impl Reader) -> Result<Self> {
        Ok(wire)
    }
}
impl Codec for u32 {
    type Wire = u32;
    fn encode(&self, _: &mut Arena) -> u32 {
        *self
    }
    fn decode(wire: u32, _: &impl Reader) -> Result<Self> {
        Ok(wire)
    }
}
impl Codec for i32 {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        *self
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        Ok(wire)
    }
}
impl Codec for u64 {
    type Wire = u64;
    fn encode(&self, _: &mut Arena) -> u64 {
        *self
    }
    fn decode(wire: u64, _: &impl Reader) -> Result<Self> {
        Ok(wire)
    }
}
impl Codec for [i32; 5] {
    type Wire = Self;
    fn encode(&self, _: &mut Arena) -> Self {
        *self
    }
    fn decode(wire: Self, _: &impl Reader) -> Result<Self> {
        Ok(wire)
    }
}
impl Codec for bool {
    type Wire = u8;
    fn encode(&self, _: &mut Arena) -> u8 {
        u8::from(*self)
    }
    fn decode(wire: u8, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for String {
    type Wire = MrString;
    fn encode(&self, arena: &mut Arena) -> MrString {
        let (data, len) = arena.store(self.as_bytes().to_vec());
        MrString { data, len }
    }
    fn decode(wire: MrString, reader: &impl Reader) -> Result<Self> {
        String::from_utf8(reader.array(wire.data, wire.len)?).map_err(|_| MR_INVALID_ARGUMENT)
    }
}
fn array_encode<T: Codec>(values: &[T], arena: &mut Arena) -> (*const T::Wire, u32)
where
    T::Wire: 'static,
{
    let data = values.iter().map(|v| v.encode(arena)).collect();
    arena.store(data)
}
fn array_decode<T: Codec>(data: *const T::Wire, len: u32, reader: &impl Reader) -> Result<Vec<T>> {
    reader
        .array(data, len)?
        .into_iter()
        .map(|v| T::decode(v, reader))
        .collect()
}
impl Codec for n::RegState {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            n::RegState::NotRegistered => 0,
            n::RegState::Home => 1,
            n::RegState::Searching => 2,
            n::RegState::Denied => 3,
            n::RegState::Unknown => 4,
            n::RegState::Roaming => 5,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(n::RegState::NotRegistered),
            1 => Ok(n::RegState::Home),
            2 => Ok(n::RegState::Searching),
            3 => Ok(n::RegState::Denied),
            4 => Ok(n::RegState::Unknown),
            5 => Ok(n::RegState::Roaming),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for n::Rat {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            n::Rat::Unknown => 0,
            n::Rat::Gsm => 16,
            n::Rat::Umts => 3,
            n::Rat::Lte => 14,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(n::Rat::Unknown),
            16 => Ok(n::Rat::Gsm),
            3 => Ok(n::Rat::Umts),
            14 => Ok(n::Rat::Lte),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for n::SelectionMode {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            n::SelectionMode::Automatic => 0,
            n::SelectionMode::Manual => 1,
            n::SelectionMode::DeregisteredUnverified => 2,
            n::SelectionMode::ManualAutomaticUnverified => 3,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(n::SelectionMode::Automatic),
            1 => Ok(n::SelectionMode::Manual),
            2 => Ok(n::SelectionMode::DeregisteredUnverified),
            3 => Ok(n::SelectionMode::ManualAutomaticUnverified),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for m::DeviceStateType {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            m::DeviceStateType::PowerSaveMode => 0,
            m::DeviceStateType::ChargingState => 1,
            m::DeviceStateType::LowDataExpected => 2,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(m::DeviceStateType::PowerSaveMode),
            1 => Ok(m::DeviceStateType::ChargingState),
            2 => Ok(m::DeviceStateType::LowDataExpected),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for m::ImeiType {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            m::ImeiType::Invalid => 0,
            m::ImeiType::Primary => 1,
            m::ImeiType::Secondary => 2,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(m::ImeiType::Invalid),
            1 => Ok(m::ImeiType::Primary),
            2 => Ok(m::ImeiType::Secondary),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for m::CardState {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            m::CardState::Absent => 0,
            m::CardState::Present => 1,
            m::CardState::Error => 2,
            m::CardState::Restricted => 3,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(m::CardState::Absent),
            1 => Ok(m::CardState::Present),
            2 => Ok(m::CardState::Error),
            3 => Ok(m::CardState::Restricted),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for s::Radio {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            s::Radio::Unavailable => 0,
            s::Radio::Off => 1,
            s::Radio::On => 2,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(s::Radio::Unavailable),
            1 => Ok(s::Radio::Off),
            2 => Ok(s::Radio::On),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for m::RadioError {
    type Wire = i32;
    fn encode(&self, _: &mut Arena) -> i32 {
        match self {
            m::RadioError::None => 0,
            m::RadioError::RadioNotAvailable => 1,
            m::RadioError::GenericFailure => 2,
            m::RadioError::RequestNotSupported => 6,
            m::RadioError::InternalErr => 38,
            m::RadioError::ModemErr => 40,
            m::RadioError::SimErr => 43,
            m::RadioError::InvalidArguments => 44,
            m::RadioError::SimBusy => 51,
            m::RadioError::OperationNotAllowed => 54,
            m::RadioError::EmptyRecord => 55,
            m::RadioError::InvalidResponse => 66,
            m::RadioError::OemError1 => 501,
        }
    }
    fn decode(wire: i32, _: &impl Reader) -> Result<Self> {
        match wire {
            0 => Ok(m::RadioError::None),
            1 => Ok(m::RadioError::RadioNotAvailable),
            2 => Ok(m::RadioError::GenericFailure),
            6 => Ok(m::RadioError::RequestNotSupported),
            38 => Ok(m::RadioError::InternalErr),
            40 => Ok(m::RadioError::ModemErr),
            43 => Ok(m::RadioError::SimErr),
            44 => Ok(m::RadioError::InvalidArguments),
            51 => Ok(m::RadioError::SimBusy),
            54 => Ok(m::RadioError::OperationNotAllowed),
            55 => Ok(m::RadioError::EmptyRecord),
            66 => Ok(m::RadioError::InvalidResponse),
            501 => Ok(m::RadioError::OemError1),
            _ => Err(MR_INVALID_ARGUMENT),
        }
    }
}
impl Codec for n::CellIdentity {
    type Wire = MrCellIdentity;
    fn encode(&self, arena: &mut Arena) -> MrCellIdentity {
        let (kind, area, cell, plmn) = match self {
            Self::Gsm { lac, cid, plmn } => (1, *lac, *cid, plmn),
            Self::Wcdma { lac, cid, plmn } => (2, *lac, *cid, plmn),
            Self::Lte { tac, ci, plmn } => (3, *tac, *ci, plmn),
        };
        MrCellIdentity {
            kind,
            area,
            cell,
            plmn: plmn.encode(arena),
        }
    }
    fn decode(w: MrCellIdentity, reader: &impl Reader) -> Result<Self> {
        if !(1..=3).contains(&w.kind) {
            return Err(MR_INVALID_ARGUMENT);
        }
        let plmn = String::decode(w.plmn, reader)?;
        Ok(match w.kind {
            1 => Self::Gsm {
                lac: w.area,
                cid: w.cell,
                plmn,
            },
            2 => Self::Wcdma {
                lac: w.area,
                cid: w.cell,
                plmn,
            },
            _ => Self::Lte {
                tac: w.area,
                ci: w.cell,
                plmn,
            },
        })
    }
}
impl Codec for n::GsmSignal {
    type Wire = MrGsmSignal;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrGsmSignal {
            signal_strength_present: u8::from(self.signal_strength.is_some()),
            signal_strength: self
                .signal_strength
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            bit_error_rate_present: u8::from(self.bit_error_rate.is_some()),
            bit_error_rate: self
                .bit_error_rate
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            signal_strength: match w.signal_strength_present {
                0 => None,
                1 => Some(<u8 as Codec>::decode(w.signal_strength, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            bit_error_rate: match w.bit_error_rate_present {
                0 => None,
                1 => Some(<u8 as Codec>::decode(w.bit_error_rate, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
        })
    }
}
impl Codec for n::LteSignal {
    type Wire = MrLteSignal;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrLteSignal {
            rsrp_present: u8::from(self.rsrp.is_some()),
            rsrp: self
                .rsrp
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            rsrq_present: u8::from(self.rsrq.is_some()),
            rsrq: self
                .rsrq
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            rssnr_present: u8::from(self.rssnr.is_some()),
            rssnr: self
                .rssnr
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            rsrp: match w.rsrp_present {
                0 => None,
                1 => Some(<i32 as Codec>::decode(w.rsrp, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            rsrq: match w.rsrq_present {
                0 => None,
                1 => Some(<i32 as Codec>::decode(w.rsrq, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            rssnr: match w.rssnr_present {
                0 => None,
                1 => Some(<i32 as Codec>::decode(w.rssnr, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
        })
    }
}
impl Codec for n::WcdmaSignal {
    type Wire = MrWcdmaSignal;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrWcdmaSignal {
            signal_strength_present: u8::from(self.signal_strength.is_some()),
            signal_strength: self
                .signal_strength
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            rscp_present: u8::from(self.rscp.is_some()),
            rscp: self
                .rscp
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            ecno_present: u8::from(self.ecno.is_some()),
            ecno: self
                .ecno
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            signal_strength: match w.signal_strength_present {
                0 => None,
                1 => Some(<u8 as Codec>::decode(w.signal_strength, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            rscp: match w.rscp_present {
                0 => None,
                1 => Some(<i32 as Codec>::decode(w.rscp, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            ecno: match w.ecno_present {
                0 => None,
                1 => Some(<i32 as Codec>::decode(w.ecno, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
        })
    }
}
impl Codec for n::SignalStrength {
    type Wire = MrSignalStrength;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrSignalStrength {
            gsm_present: u8::from(self.gsm.is_some()),
            gsm: self
                .gsm
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            lte_present: u8::from(self.lte.is_some()),
            lte: self
                .lte
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            wcdma_present: u8::from(self.wcdma.is_some()),
            wcdma: self
                .wcdma
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            gsm: match w.gsm_present {
                0 => None,
                1 => Some(<n::GsmSignal as Codec>::decode(w.gsm, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            lte: match w.lte_present {
                0 => None,
                1 => Some(<n::LteSignal as Codec>::decode(w.lte, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            wcdma: match w.wcdma_present {
                0 => None,
                1 => Some(<n::WcdmaSignal as Codec>::decode(w.wcdma, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
        })
    }
}
impl Codec for n::RegStateResult {
    type Wire = MrRegStateResult;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrRegStateResult {
            reg_state: self.reg_state.encode(arena),
            rat: self.rat.encode(arena),
            cell_identity_present: u8::from(self.cell_identity.is_some()),
            cell_identity: self
                .cell_identity
                .as_ref()
                .map(|v| v.encode(arena))
                .unwrap_or_default(),
            reason_for_denial: self.reason_for_denial.encode(arena),
            registered_plmn: self.registered_plmn.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            reg_state: <n::RegState as Codec>::decode(w.reg_state, reader)?,
            rat: <n::Rat as Codec>::decode(w.rat, reader)?,
            cell_identity: match w.cell_identity_present {
                0 => None,
                1 => Some(<n::CellIdentity as Codec>::decode(w.cell_identity, reader)?),
                _ => return Err(MR_INVALID_ARGUMENT),
            },
            reason_for_denial: <i32 as Codec>::decode(w.reason_for_denial, reader)?,
            registered_plmn: <String as Codec>::decode(w.registered_plmn, reader)?,
        })
    }
}
impl Codec for n::OperatorInfo {
    type Wire = MrOperatorInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrOperatorInfo {
            alpha_long: self.alpha_long.encode(arena),
            alpha_short: self.alpha_short.encode(arena),
            numeric: self.numeric.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            alpha_long: <String as Codec>::decode(w.alpha_long, reader)?,
            alpha_short: <String as Codec>::decode(w.alpha_short, reader)?,
            numeric: <String as Codec>::decode(w.numeric, reader)?,
        })
    }
}
impl Codec for n::CellInfo {
    type Wire = MrCellInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrCellInfo {
            registered: self.registered.encode(arena),
            identity: self.identity.encode(arena),
            signal: self.signal.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            registered: <bool as Codec>::decode(w.registered, reader)?,
            identity: <n::CellIdentity as Codec>::decode(w.identity, reader)?,
            signal: <n::SignalStrength as Codec>::decode(w.signal, reader)?,
        })
    }
}
impl Codec for n::SignalCriteria {
    type Wire = MrSignalCriteria;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (thresholds_data, thresholds_len) = array_encode(&self.thresholds, arena);
        MrSignalCriteria {
            access_network: self.access_network.encode(arena),
            measurement: self.measurement.encode(arena),
            enabled: self.enabled.encode(arena),
            hysteresis_ms: self.hysteresis_ms.encode(arena),
            hysteresis_db: self.hysteresis_db.encode(arena),
            thresholds_data,
            thresholds_len,
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            access_network: <i32 as Codec>::decode(w.access_network, reader)?,
            measurement: <i32 as Codec>::decode(w.measurement, reader)?,
            enabled: <bool as Codec>::decode(w.enabled, reader)?,
            hysteresis_ms: <i32 as Codec>::decode(w.hysteresis_ms, reader)?,
            hysteresis_db: <i32 as Codec>::decode(w.hysteresis_db, reader)?,
            thresholds: array_decode::<i32>(w.thresholds_data, w.thresholds_len, reader)?,
        })
    }
}
impl Codec for n::LinkCriteria {
    type Wire = MrLinkCriteria;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (thresholds_dl_kbps_data, thresholds_dl_kbps_len) =
            array_encode(&self.thresholds_dl_kbps, arena);
        let (thresholds_ul_kbps_data, thresholds_ul_kbps_len) =
            array_encode(&self.thresholds_ul_kbps, arena);
        MrLinkCriteria {
            access_network: self.access_network.encode(arena),
            hysteresis_ms: self.hysteresis_ms.encode(arena),
            hysteresis_dl_kbps: self.hysteresis_dl_kbps.encode(arena),
            hysteresis_ul_kbps: self.hysteresis_ul_kbps.encode(arena),
            thresholds_dl_kbps_data,
            thresholds_dl_kbps_len,
            thresholds_ul_kbps_data,
            thresholds_ul_kbps_len,
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            access_network: <i32 as Codec>::decode(w.access_network, reader)?,
            hysteresis_ms: <i32 as Codec>::decode(w.hysteresis_ms, reader)?,
            hysteresis_dl_kbps: <i32 as Codec>::decode(w.hysteresis_dl_kbps, reader)?,
            hysteresis_ul_kbps: <i32 as Codec>::decode(w.hysteresis_ul_kbps, reader)?,
            thresholds_dl_kbps: array_decode::<i32>(
                w.thresholds_dl_kbps_data,
                w.thresholds_dl_kbps_len,
                reader,
            )?,
            thresholds_ul_kbps: array_decode::<i32>(
                w.thresholds_ul_kbps_data,
                w.thresholds_ul_kbps_len,
                reader,
            )?,
        })
    }
}
impl Codec for m::ImeiInfo {
    type Wire = MrImeiInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrImeiInfo {
            kind: self.kind.encode(arena),
            imei: self.imei.encode(arena),
            svn: self.svn.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            kind: <m::ImeiType as Codec>::decode(w.kind, reader)?,
            imei: <String as Codec>::decode(w.imei, reader)?,
            svn: <String as Codec>::decode(w.svn, reader)?,
        })
    }
}
impl Codec for m::DeviceIdentity {
    type Wire = MrDeviceIdentity;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrDeviceIdentity {
            imei: self.imei.encode(arena),
            imeisv: self.imeisv.encode(arena),
            esn: self.esn.encode(arena),
            meid: self.meid.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            imei: <String as Codec>::decode(w.imei, reader)?,
            imeisv: <String as Codec>::decode(w.imeisv, reader)?,
            esn: <String as Codec>::decode(w.esn, reader)?,
            meid: <String as Codec>::decode(w.meid, reader)?,
        })
    }
}
impl Codec for m::ActivityStatsTechSpecificInfo {
    type Wire = MrActivityStatsTechSpecificInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrActivityStatsTechSpecificInfo {
            rat: self.rat.encode(arena),
            frequency_range: self.frequency_range.encode(arena),
            txm_mode_time_ms: self.txm_mode_time_ms.encode(arena),
            rx_mode_time_ms: self.rx_mode_time_ms.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            rat: <i32 as Codec>::decode(w.rat, reader)?,
            frequency_range: <i32 as Codec>::decode(w.frequency_range, reader)?,
            txm_mode_time_ms: <[i32; 5] as Codec>::decode(w.txm_mode_time_ms, reader)?,
            rx_mode_time_ms: <i32 as Codec>::decode(w.rx_mode_time_ms, reader)?,
        })
    }
}
impl Codec for m::ActivityStatsInfo {
    type Wire = MrActivityStatsInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (tech_specific_info_data, tech_specific_info_len) =
            array_encode(&self.tech_specific_info, arena);
        MrActivityStatsInfo {
            sleep_mode_time_ms: self.sleep_mode_time_ms.encode(arena),
            idle_mode_time_ms: self.idle_mode_time_ms.encode(arena),
            tech_specific_info_data,
            tech_specific_info_len,
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            sleep_mode_time_ms: <i32 as Codec>::decode(w.sleep_mode_time_ms, reader)?,
            idle_mode_time_ms: <i32 as Codec>::decode(w.idle_mode_time_ms, reader)?,
            tech_specific_info: array_decode::<m::ActivityStatsTechSpecificInfo>(
                w.tech_specific_info_data,
                w.tech_specific_info_len,
                reader,
            )?,
        })
    }
}
impl Codec for m::RadioCapability {
    type Wire = MrRadioCapability;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrRadioCapability {
            session: self.session.encode(arena),
            phase: self.phase.encode(arena),
            raf: self.raf.encode(arena),
            logical_modem_uuid: self.logical_modem_uuid.encode(arena),
            status: self.status.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            session: <i32 as Codec>::decode(w.session, reader)?,
            phase: <i32 as Codec>::decode(w.phase, reader)?,
            raf: <i32 as Codec>::decode(w.raf, reader)?,
            logical_modem_uuid: <String as Codec>::decode(w.logical_modem_uuid, reader)?,
            status: <i32 as Codec>::decode(w.status, reader)?,
        })
    }
}
impl Codec for m::SlotPortMapping {
    type Wire = MrSlotPortMapping;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrSlotPortMapping {
            physical_slot_id: self.physical_slot_id.encode(arena),
            port_id: self.port_id.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            physical_slot_id: <i32 as Codec>::decode(w.physical_slot_id, reader)?,
            port_id: <i32 as Codec>::decode(w.port_id, reader)?,
        })
    }
}
impl Codec for m::AppStatus {
    type Wire = MrAppStatus;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrAppStatus {
            app_type: self.app_type.encode(arena),
            app_state: self.app_state.encode(arena),
            perso_substate: self.perso_substate.encode(arena),
            aid_ptr: self.aid_ptr.encode(arena),
            app_label_ptr: self.app_label_ptr.encode(arena),
            pin1_replaced: self.pin1_replaced.encode(arena),
            pin1: self.pin1.encode(arena),
            pin2: self.pin2.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            app_type: <i32 as Codec>::decode(w.app_type, reader)?,
            app_state: <i32 as Codec>::decode(w.app_state, reader)?,
            perso_substate: <i32 as Codec>::decode(w.perso_substate, reader)?,
            aid_ptr: <String as Codec>::decode(w.aid_ptr, reader)?,
            app_label_ptr: <String as Codec>::decode(w.app_label_ptr, reader)?,
            pin1_replaced: <bool as Codec>::decode(w.pin1_replaced, reader)?,
            pin1: <i32 as Codec>::decode(w.pin1, reader)?,
            pin2: <i32 as Codec>::decode(w.pin2, reader)?,
        })
    }
}
impl Codec for m::CardStatus {
    type Wire = MrCardStatus;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (applications_data, applications_len) = array_encode(&self.applications, arena);
        MrCardStatus {
            card_state: self.card_state.encode(arena),
            universal_pin_state: self.universal_pin_state.encode(arena),
            gsm_umts_subscription_app_index: self.gsm_umts_subscription_app_index.encode(arena),
            cdma_subscription_app_index: self.cdma_subscription_app_index.encode(arena),
            ims_subscription_app_index: self.ims_subscription_app_index.encode(arena),
            applications_data,
            applications_len,
            atr: self.atr.encode(arena),
            iccid: self.iccid.encode(arena),
            eid: self.eid.encode(arena),
            slot_map: self.slot_map.encode(arena),
            supported_mep_mode: self.supported_mep_mode.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            card_state: <m::CardState as Codec>::decode(w.card_state, reader)?,
            universal_pin_state: <i32 as Codec>::decode(w.universal_pin_state, reader)?,
            gsm_umts_subscription_app_index: <i32 as Codec>::decode(
                w.gsm_umts_subscription_app_index,
                reader,
            )?,
            cdma_subscription_app_index: <i32 as Codec>::decode(
                w.cdma_subscription_app_index,
                reader,
            )?,
            ims_subscription_app_index: <i32 as Codec>::decode(
                w.ims_subscription_app_index,
                reader,
            )?,
            applications: array_decode::<m::AppStatus>(
                w.applications_data,
                w.applications_len,
                reader,
            )?,
            atr: <String as Codec>::decode(w.atr, reader)?,
            iccid: <String as Codec>::decode(w.iccid, reader)?,
            eid: <String as Codec>::decode(w.eid, reader)?,
            slot_map: <m::SlotPortMapping as Codec>::decode(w.slot_map, reader)?,
            supported_mep_mode: <i32 as Codec>::decode(w.supported_mep_mode, reader)?,
        })
    }
}
impl Codec for m::SimPortInfo {
    type Wire = MrSimPortInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrSimPortInfo {
            icc_id: self.icc_id.encode(arena),
            logical_slot_id: self.logical_slot_id.encode(arena),
            port_active: self.port_active.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            icc_id: <String as Codec>::decode(w.icc_id, reader)?,
            logical_slot_id: <i32 as Codec>::decode(w.logical_slot_id, reader)?,
            port_active: <bool as Codec>::decode(w.port_active, reader)?,
        })
    }
}
impl Codec for m::SimSlotStatus {
    type Wire = MrSimSlotStatus;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (port_info_data, port_info_len) = array_encode(&self.port_info, arena);
        MrSimSlotStatus {
            card_state: self.card_state.encode(arena),
            atr: self.atr.encode(arena),
            eid: self.eid.encode(arena),
            port_info_data,
            port_info_len,
            supported_mep_mode: self.supported_mep_mode.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            card_state: <m::CardState as Codec>::decode(w.card_state, reader)?,
            atr: <String as Codec>::decode(w.atr, reader)?,
            eid: <String as Codec>::decode(w.eid, reader)?,
            port_info: array_decode::<m::SimPortInfo>(w.port_info_data, w.port_info_len, reader)?,
            supported_mep_mode: <i32 as Codec>::decode(w.supported_mep_mode, reader)?,
        })
    }
}
impl Codec for m::PhoneCapability {
    type Wire = MrPhoneCapability;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (logical_modem_ids_data, logical_modem_ids_len) =
            array_encode(&self.logical_modem_ids, arena);
        MrPhoneCapability {
            max_active_data: self.max_active_data.encode(arena),
            max_active_internet_data: self.max_active_internet_data.encode(arena),
            is_internet_lingering_supported: self.is_internet_lingering_supported.encode(arena),
            logical_modem_ids_data,
            logical_modem_ids_len,
            max_active_voice: self.max_active_voice.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            max_active_data: <i8 as Codec>::decode(w.max_active_data, reader)?,
            max_active_internet_data: <i8 as Codec>::decode(w.max_active_internet_data, reader)?,
            is_internet_lingering_supported: <bool as Codec>::decode(
                w.is_internet_lingering_supported,
                reader,
            )?,
            logical_modem_ids: array_decode::<i8>(
                w.logical_modem_ids_data,
                w.logical_modem_ids_len,
                reader,
            )?,
            max_active_voice: <i8 as Codec>::decode(w.max_active_voice, reader)?,
        })
    }
}
impl Codec for m::GsmBroadcastSmsConfigInfo {
    type Wire = MrGsmBroadcastSmsConfigInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrGsmBroadcastSmsConfigInfo {
            from_service_id: self.from_service_id.encode(arena),
            to_service_id: self.to_service_id.encode(arena),
            from_code_scheme: self.from_code_scheme.encode(arena),
            to_code_scheme: self.to_code_scheme.encode(arena),
            selected: self.selected.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            from_service_id: <i32 as Codec>::decode(w.from_service_id, reader)?,
            to_service_id: <i32 as Codec>::decode(w.to_service_id, reader)?,
            from_code_scheme: <i32 as Codec>::decode(w.from_code_scheme, reader)?,
            to_code_scheme: <i32 as Codec>::decode(w.to_code_scheme, reader)?,
            selected: <bool as Codec>::decode(w.selected, reader)?,
        })
    }
}
impl Codec for m::UusInfo {
    type Wire = MrUusInfo;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        MrUusInfo {
            uus_type: self.uus_type.encode(arena),
            uus_dcs: self.uus_dcs.encode(arena),
            uus_data: self.uus_data.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            uus_type: <i32 as Codec>::decode(w.uus_type, reader)?,
            uus_dcs: <i32 as Codec>::decode(w.uus_dcs, reader)?,
            uus_data: <String as Codec>::decode(w.uus_data, reader)?,
        })
    }
}
impl Codec for m::Call {
    type Wire = MrCall;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        let (uus_info_data, uus_info_len) = array_encode(&self.uus_info, arena);
        MrCall {
            state: self.state.encode(arena),
            index: self.index.encode(arena),
            toa: self.toa.encode(arena),
            is_mpty: self.is_mpty.encode(arena),
            is_mt: self.is_mt.encode(arena),
            als: self.als.encode(arena),
            is_voice: self.is_voice.encode(arena),
            is_voice_privacy: self.is_voice_privacy.encode(arena),
            number: self.number.encode(arena),
            number_presentation: self.number_presentation.encode(arena),
            name: self.name.encode(arena),
            name_presentation: self.name_presentation.encode(arena),
            uus_info_data,
            uus_info_len,
            audio_quality: self.audio_quality.encode(arena),
            forwarded_number: self.forwarded_number.encode(arena),
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(Self {
            state: <i32 as Codec>::decode(w.state, reader)?,
            index: <i32 as Codec>::decode(w.index, reader)?,
            toa: <i32 as Codec>::decode(w.toa, reader)?,
            is_mpty: <bool as Codec>::decode(w.is_mpty, reader)?,
            is_mt: <bool as Codec>::decode(w.is_mt, reader)?,
            als: <i8 as Codec>::decode(w.als, reader)?,
            is_voice: <bool as Codec>::decode(w.is_voice, reader)?,
            is_voice_privacy: <bool as Codec>::decode(w.is_voice_privacy, reader)?,
            number: <String as Codec>::decode(w.number, reader)?,
            number_presentation: <i32 as Codec>::decode(w.number_presentation, reader)?,
            name: <String as Codec>::decode(w.name, reader)?,
            name_presentation: <i32 as Codec>::decode(w.name_presentation, reader)?,
            uus_info: array_decode::<m::UusInfo>(w.uus_info_data, w.uus_info_len, reader)?,
            audio_quality: <i32 as Codec>::decode(w.audio_quality, reader)?,
            forwarded_number: <String as Codec>::decode(w.forwarded_number, reader)?,
        })
    }
}
impl Codec for Request {
    type Wire = MrRequest;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        match self {
            Request::Network(n::Request::GetVoiceRegistrationState) => MrRequest {
                kind: 1,
                ..Default::default()
            },
            Request::Network(n::Request::GetDataRegistrationState) => MrRequest {
                kind: 2,
                ..Default::default()
            },
            Request::Network(n::Request::GetOperator) => MrRequest {
                kind: 3,
                ..Default::default()
            },
            Request::Network(n::Request::GetNetworkSelectionMode) => MrRequest {
                kind: 4,
                ..Default::default()
            },
            Request::Network(n::Request::GetSignalStrength) => MrRequest {
                kind: 5,
                ..Default::default()
            },
            Request::Network(n::Request::GetCellInfoList) => MrRequest {
                kind: 6,
                ..Default::default()
            },
            Request::Network(n::Request::GetImsRegistrationState) => MrRequest {
                kind: 7,
                ..Default::default()
            },
            Request::Network(n::Request::SetIndicationFilter(v)) => MrRequest {
                kind: 8,
                mask: v.encode(arena),
                ..Default::default()
            },
            Request::Network(n::Request::SetSignalStrengthReportingCriteria(v)) => {
                let (signal_criteria_data, signal_criteria_len) = array_encode(v, arena);
                MrRequest {
                    kind: 9,
                    signal_criteria_data,
                    signal_criteria_len,
                    ..Default::default()
                }
            }
            Request::Network(n::Request::SetLinkCapacityReportingCriteria(v)) => MrRequest {
                kind: 10,
                link_criteria: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SetRadioPower {
                on,
                for_emergency_call,
                preferred_for_emergency_call,
            }) => MrRequest {
                kind: 101,
                power_on: on.encode(arena),
                for_emergency_call: for_emergency_call.encode(arena),
                preferred_for_emergency_call: preferred_for_emergency_call.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::GetBasebandVersion) => MrRequest {
                kind: 102,
                ..Default::default()
            },
            Request::Modem(m::Request::GetImei) => MrRequest {
                kind: 103,
                ..Default::default()
            },
            Request::Modem(m::Request::GetDeviceIdentity) => MrRequest {
                kind: 104,
                ..Default::default()
            },
            Request::Modem(m::Request::GetModemActivityInfo) => MrRequest {
                kind: 105,
                ..Default::default()
            },
            Request::Modem(m::Request::GetModemStackStatus) => MrRequest {
                kind: 106,
                ..Default::default()
            },
            Request::Modem(m::Request::EnableModem(v)) => MrRequest {
                kind: 107,
                boolean: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SendDeviceState(kind, state)) => MrRequest {
                kind: 108,
                device_state_type: kind.encode(arena),
                boolean: state.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::GetRadioCapability) => MrRequest {
                kind: 109,
                ..Default::default()
            },
            Request::Modem(m::Request::GetIccCardStatus) => MrRequest {
                kind: 110,
                ..Default::default()
            },
            Request::Modem(m::Request::GetUiccApplicationsEnablement) => MrRequest {
                kind: 111,
                ..Default::default()
            },
            Request::Modem(m::Request::AreUiccApplicationsEnabled) => MrRequest {
                kind: 112,
                ..Default::default()
            },
            Request::Modem(m::Request::EnableUiccApplications(v)) => MrRequest {
                kind: 113,
                boolean: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SetCarrierInfoForImsiEncryption) => MrRequest {
                kind: 114,
                ..Default::default()
            },
            Request::Modem(m::Request::GetHalDeviceCapabilities) => MrRequest {
                kind: 115,
                ..Default::default()
            },
            Request::Modem(m::Request::GetSimSlotsStatus) => MrRequest {
                kind: 116,
                ..Default::default()
            },
            Request::Modem(m::Request::GetPhoneCapability) => MrRequest {
                kind: 117,
                ..Default::default()
            },
            Request::Modem(m::Request::GetNumOfLiveModems) => MrRequest {
                kind: 118,
                ..Default::default()
            },
            Request::Modem(m::Request::SetNumOfLiveModems(v)) => MrRequest {
                kind: 119,
                small_integer: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SetPreferredDataModem(v)) => MrRequest {
                kind: 120,
                small_integer: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::GetCurrentCalls) => MrRequest {
                kind: 121,
                ..Default::default()
            },
            Request::Modem(m::Request::SetTtyMode(v)) => MrRequest {
                kind: 122,
                integer: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SetGsmBroadcastConfig(v)) => {
                let (broadcast_config_data, broadcast_config_len) = array_encode(v, arena);
                MrRequest {
                    kind: 123,
                    broadcast_config_data,
                    broadcast_config_len,
                    ..Default::default()
                }
            }
            Request::Modem(m::Request::SetDataProfile { profile_count }) => MrRequest {
                kind: 124,
                profile_count: u64::try_from(*profile_count).expect("profile count"),
                ..Default::default()
            },
            Request::Modem(m::Request::SetNullCipherAndIntegrityEnabled(v)) => MrRequest {
                kind: 125,
                boolean: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SetSecurityAlgorithmsUpdatedEnabled(v)) => MrRequest {
                kind: 126,
                boolean: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::SetCellularIdentifierDisclosedEnabled(v)) => MrRequest {
                kind: 127,
                boolean: v.encode(arena),
                ..Default::default()
            },
            Request::Modem(m::Request::IsSatelliteEnabledForCarrier) => MrRequest {
                kind: 128,
                ..Default::default()
            },
            Request::Modem(m::Request::GetBarringInfo) => MrRequest {
                kind: 129,
                ..Default::default()
            },
            Request::Modem(m::Request::GetVoiceRadioTechnology) => MrRequest {
                kind: 130,
                ..Default::default()
            },
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(match w.kind {
            1 => Request::Network(n::Request::GetVoiceRegistrationState),
            2 => Request::Network(n::Request::GetDataRegistrationState),
            3 => Request::Network(n::Request::GetOperator),
            4 => Request::Network(n::Request::GetNetworkSelectionMode),
            5 => Request::Network(n::Request::GetSignalStrength),
            6 => Request::Network(n::Request::GetCellInfoList),
            7 => Request::Network(n::Request::GetImsRegistrationState),
            8 => Request::Network(n::Request::SetIndicationFilter(<u32 as Codec>::decode(
                w.mask, reader,
            )?)),
            9 => Request::Network(n::Request::SetSignalStrengthReportingCriteria(
                array_decode::<n::SignalCriteria>(
                    w.signal_criteria_data,
                    w.signal_criteria_len,
                    reader,
                )?,
            )),
            10 => Request::Network(n::Request::SetLinkCapacityReportingCriteria(
                <n::LinkCriteria as Codec>::decode(w.link_criteria, reader)?,
            )),
            101 => Request::Modem(m::Request::SetRadioPower {
                on: bool::decode(w.power_on, reader)?,
                for_emergency_call: bool::decode(w.for_emergency_call, reader)?,
                preferred_for_emergency_call: bool::decode(w.preferred_for_emergency_call, reader)?,
            }),
            102 => Request::Modem(m::Request::GetBasebandVersion),
            103 => Request::Modem(m::Request::GetImei),
            104 => Request::Modem(m::Request::GetDeviceIdentity),
            105 => Request::Modem(m::Request::GetModemActivityInfo),
            106 => Request::Modem(m::Request::GetModemStackStatus),
            107 => Request::Modem(m::Request::EnableModem(<bool as Codec>::decode(
                w.boolean, reader,
            )?)),
            108 => Request::Modem(m::Request::SendDeviceState(
                m::DeviceStateType::decode(w.device_state_type, reader)?,
                bool::decode(w.boolean, reader)?,
            )),
            109 => Request::Modem(m::Request::GetRadioCapability),
            110 => Request::Modem(m::Request::GetIccCardStatus),
            111 => Request::Modem(m::Request::GetUiccApplicationsEnablement),
            112 => Request::Modem(m::Request::AreUiccApplicationsEnabled),
            113 => Request::Modem(m::Request::EnableUiccApplications(<bool as Codec>::decode(
                w.boolean, reader,
            )?)),
            114 => Request::Modem(m::Request::SetCarrierInfoForImsiEncryption),
            115 => Request::Modem(m::Request::GetHalDeviceCapabilities),
            116 => Request::Modem(m::Request::GetSimSlotsStatus),
            117 => Request::Modem(m::Request::GetPhoneCapability),
            118 => Request::Modem(m::Request::GetNumOfLiveModems),
            119 => Request::Modem(m::Request::SetNumOfLiveModems(<i8 as Codec>::decode(
                w.small_integer,
                reader,
            )?)),
            120 => Request::Modem(m::Request::SetPreferredDataModem(<i8 as Codec>::decode(
                w.small_integer,
                reader,
            )?)),
            121 => Request::Modem(m::Request::GetCurrentCalls),
            122 => Request::Modem(m::Request::SetTtyMode(<i32 as Codec>::decode(
                w.integer, reader,
            )?)),
            123 => Request::Modem(m::Request::SetGsmBroadcastConfig(array_decode::<
                m::GsmBroadcastSmsConfigInfo,
            >(
                w.broadcast_config_data,
                w.broadcast_config_len,
                reader,
            )?)),
            124 => Request::Modem(m::Request::SetDataProfile {
                profile_count: usize::try_from(w.profile_count).map_err(|_| MR_INVALID_ARGUMENT)?,
            }),
            125 => Request::Modem(m::Request::SetNullCipherAndIntegrityEnabled(
                <bool as Codec>::decode(w.boolean, reader)?,
            )),
            126 => Request::Modem(m::Request::SetSecurityAlgorithmsUpdatedEnabled(
                <bool as Codec>::decode(w.boolean, reader)?,
            )),
            127 => Request::Modem(m::Request::SetCellularIdentifierDisclosedEnabled(
                <bool as Codec>::decode(w.boolean, reader)?,
            )),
            128 => Request::Modem(m::Request::IsSatelliteEnabledForCarrier),
            129 => Request::Modem(m::Request::GetBarringInfo),
            130 => Request::Modem(m::Request::GetVoiceRadioTechnology),
            _ => return Err(MR_INVALID_ARGUMENT),
        })
    }
}
impl Codec for Value {
    type Wire = MrValue;
    fn encode(&self, arena: &mut Arena) -> Self::Wire {
        match self {
            Value::Network(n::Value::Void) => MrValue {
                kind: 1,
                ..Default::default()
            },
            Value::Network(n::Value::Registration(v)) => MrValue {
                kind: 2,
                registration: v.encode(arena),
                ..Default::default()
            },
            Value::Network(n::Value::Operator(v)) => MrValue {
                kind: 3,
                operator_info: v.encode(arena),
                ..Default::default()
            },
            Value::Network(n::Value::Selection(v)) => MrValue {
                kind: 4,
                selection: v.encode(arena),
                ..Default::default()
            },
            Value::Network(n::Value::Signal(v)) => MrValue {
                kind: 5,
                signal: v.encode(arena),
                ..Default::default()
            },
            Value::Network(n::Value::Cells(v)) => {
                let (cells_data, cells_len) = array_encode(v, arena);
                MrValue {
                    kind: 6,
                    cells_data,
                    cells_len,
                    ..Default::default()
                }
            }
            Value::Modem(m::Value::Void) => MrValue {
                kind: 101,
                ..Default::default()
            },
            Value::Modem(m::Value::Baseband(v)) => MrValue {
                kind: 102,
                text: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::Imei(v)) => MrValue {
                kind: 103,
                imei: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::Identity(v)) => MrValue {
                kind: 104,
                identity: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::Activity(v)) => MrValue {
                kind: 105,
                activity: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::Capability(v)) => MrValue {
                kind: 106,
                capability: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::Card(v)) => MrValue {
                kind: 107,
                card: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::Slots(v)) => {
                let (slots_data, slots_len) = array_encode(v, arena);
                MrValue {
                    kind: 108,
                    slots_data,
                    slots_len,
                    ..Default::default()
                }
            }
            Value::Modem(m::Value::Phone(v)) => MrValue {
                kind: 109,
                phone: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::ReducedFeatureSet1(v)) => MrValue {
                kind: 110,
                boolean: v.encode(arena),
                ..Default::default()
            },
            Value::Modem(m::Value::CurrentCalls(v)) => {
                let (calls_data, calls_len) = array_encode(v, arena);
                MrValue {
                    kind: 111,
                    calls_data,
                    calls_len,
                    ..Default::default()
                }
            }
            Value::Modem(m::Value::VoiceRadioTechnology(v)) => MrValue {
                kind: 112,
                integer: v.encode(arena),
                ..Default::default()
            },
        }
    }
    fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {
        Ok(match w.kind {
            1 => Value::Network(n::Value::Void),
            2 => Value::Network(n::Value::Registration(
                <n::RegStateResult as Codec>::decode(w.registration, reader)?,
            )),
            3 => Value::Network(n::Value::Operator(<n::OperatorInfo as Codec>::decode(
                w.operator_info,
                reader,
            )?)),
            4 => Value::Network(n::Value::Selection(<n::SelectionMode as Codec>::decode(
                w.selection,
                reader,
            )?)),
            5 => Value::Network(n::Value::Signal(<n::SignalStrength as Codec>::decode(
                w.signal, reader,
            )?)),
            6 => Value::Network(n::Value::Cells(array_decode::<n::CellInfo>(
                w.cells_data,
                w.cells_len,
                reader,
            )?)),
            101 => Value::Modem(m::Value::Void),
            102 => Value::Modem(m::Value::Baseband(<String as Codec>::decode(
                w.text, reader,
            )?)),
            103 => Value::Modem(m::Value::Imei(<m::ImeiInfo as Codec>::decode(
                w.imei, reader,
            )?)),
            104 => Value::Modem(m::Value::Identity(<m::DeviceIdentity as Codec>::decode(
                w.identity, reader,
            )?)),
            105 => Value::Modem(m::Value::Activity(<m::ActivityStatsInfo as Codec>::decode(
                w.activity, reader,
            )?)),
            106 => Value::Modem(m::Value::Capability(<m::RadioCapability as Codec>::decode(
                w.capability,
                reader,
            )?)),
            107 => Value::Modem(m::Value::Card(<m::CardStatus as Codec>::decode(
                w.card, reader,
            )?)),
            108 => Value::Modem(m::Value::Slots(array_decode::<m::SimSlotStatus>(
                w.slots_data,
                w.slots_len,
                reader,
            )?)),
            109 => Value::Modem(m::Value::Phone(<m::PhoneCapability as Codec>::decode(
                w.phone, reader,
            )?)),
            110 => Value::Modem(m::Value::ReducedFeatureSet1(<bool as Codec>::decode(
                w.boolean, reader,
            )?)),
            111 => Value::Modem(m::Value::CurrentCalls(array_decode::<m::Call>(
                w.calls_data,
                w.calls_len,
                reader,
            )?)),
            112 => Value::Modem(m::Value::VoiceRadioTechnology(<i32 as Codec>::decode(
                w.integer, reader,
            )?)),
            _ => return Err(MR_INVALID_ARGUMENT),
        })
    }
}
impl Codec for Indication {
    type Wire = MrIndication;
    fn encode(&self, arena: &mut Arena) -> MrIndication {
        match self {
            Self::NetworkStateChanged => MrIndication {
                kind: 1,
                ..Default::default()
            },
            Self::CurrentSignalStrength(v) => MrIndication {
                kind: 2,
                signal: v.encode(arena),
                ..Default::default()
            },
            Self::RadioStateChanged(v) => MrIndication {
                kind: 3,
                radio_state: v.encode(arena),
                ..Default::default()
            },
            Self::SimStatusChanged => MrIndication {
                kind: 4,
                ..Default::default()
            },
            Self::ModemReset { generation } => MrIndication {
                kind: 5,
                generation: *generation,
                ..Default::default()
            },
        }
    }
    fn decode(w: MrIndication, reader: &impl Reader) -> Result<Self> {
        Ok(match w.kind {
            1 => Self::NetworkStateChanged,
            2 => Self::CurrentSignalStrength(n::SignalStrength::decode(w.signal, reader)?),
            3 => Self::RadioStateChanged(s::Radio::decode(w.radio_state, reader)?),
            4 => Self::SimStatusChanged,
            5 => Self::ModemReset {
                generation: w.generation,
            },
            _ => return Err(MR_INVALID_ARGUMENT),
        })
    }
}
impl Codec for Delivery {
    type Wire = MrOutput;
    fn encode(&self, arena: &mut Arena) -> MrOutput {
        match self {
            Self::Response {
                invocation,
                error,
                value,
            } => MrOutput {
                kind: 1,
                invocation_id: *invocation,
                error: error.encode(arena),
                value_present: u8::from(value.is_some()),
                value: value.as_ref().map(|v| v.encode(arena)).unwrap_or_default(),
                ..Default::default()
            },
            Self::Indication(v) => MrOutput {
                kind: 2,
                indication: v.encode(arena),
                ..Default::default()
            },
        }
    }
    fn decode(w: MrOutput, reader: &impl Reader) -> Result<Self> {
        Ok(match w.kind {
            1 => Self::Response {
                invocation: w.invocation_id,
                error: m::RadioError::decode(w.error, reader)?,
                value: match w.value_present {
                    0 => None,
                    1 => Some(Value::decode(w.value, reader)?),
                    _ => return Err(MR_INVALID_ARGUMENT),
                },
            },
            2 => Self::Indication(Indication::decode(w.indication, reader)?),
            _ => return Err(MR_INVALID_ARGUMENT),
        })
    }
}
