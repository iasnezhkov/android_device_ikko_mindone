# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

from pathlib import Path
import argparse
import re
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
SPDX = '/*\n * SPDX-License-Identifier: Apache-2.0\n * SPDX-FileCopyrightText: 2026 The LineageOS Project\n */\n\n'
SCALARS = {'u8': 'uint8_t', 'i8': 'int8_t', 'u32': 'uint32_t', 'i32': 'int32_t', 'u64': 'uint64_t'}
ENUMS = {
    'n::RegState': [('NotRegistered', 0), ('Home', 1), ('Searching', 2), ('Denied', 3), ('Unknown', 4), ('Roaming', 5)],
    'n::Rat': [('Unknown', 0), ('Gsm', 16), ('Umts', 3), ('Lte', 14)],
    'n::SelectionMode': [('Automatic', 0), ('Manual', 1), ('DeregisteredUnverified', 2), ('ManualAutomaticUnverified', 3)],
    'm::DeviceStateType': [('PowerSaveMode', 0), ('ChargingState', 1), ('LowDataExpected', 2)],
    'm::ImeiType': [('Invalid', 0), ('Primary', 1), ('Secondary', 2)],
    'm::CardState': [('Absent', 0), ('Present', 1), ('Error', 2), ('Restricted', 3)],
    's::Radio': [('Unavailable', 0), ('Off', 1), ('On', 2)],
    'm::RadioError': [('None', 0), ('RadioNotAvailable', 1), ('GenericFailure', 2), ('RequestNotSupported', 6), ('InternalErr', 38), ('ModemErr', 40), ('SimErr', 43), ('InvalidArguments', 44), ('SimBusy', 51), ('OperationNotAllowed', 54), ('EmptyRecord', 55), ('InvalidResponse', 66), ('OemError1', 501)],
}
MODELS = [
    ('n::GsmSignal', [('signal_strength', '?u8'), ('bit_error_rate', '?u8')]),
    ('n::LteSignal', [('rsrp', '?i32'), ('rsrq', '?i32'), ('rssnr', '?i32')]),
    ('n::WcdmaSignal', [('signal_strength', '?u8'), ('rscp', '?i32'), ('ecno', '?i32')]),
    ('n::SignalStrength', [('gsm', '?n::GsmSignal'), ('lte', '?n::LteSignal'), ('wcdma', '?n::WcdmaSignal')]),
    ('n::RegStateResult', [('reg_state', 'n::RegState'), ('rat', 'n::Rat'), ('cell_identity', '?n::CellIdentity'), ('reason_for_denial', 'i32'), ('registered_plmn', 'String')]),
    ('n::OperatorInfo', [('alpha_long', 'String'), ('alpha_short', 'String'), ('numeric', 'String')]),
    ('n::CellInfo', [('registered', 'bool'), ('identity', 'n::CellIdentity'), ('signal', 'n::SignalStrength')]),
    ('n::SignalCriteria', [('access_network', 'i32'), ('measurement', 'i32'), ('enabled', 'bool'), ('hysteresis_ms', 'i32'), ('hysteresis_db', 'i32'), ('thresholds', '*i32')]),
    ('n::LinkCriteria', [('access_network', 'i32'), ('hysteresis_ms', 'i32'), ('hysteresis_dl_kbps', 'i32'), ('hysteresis_ul_kbps', 'i32'), ('thresholds_dl_kbps', '*i32'), ('thresholds_ul_kbps', '*i32')]),
    ('m::ImeiInfo', [('kind', 'm::ImeiType'), ('imei', 'String'), ('svn', 'String')]),
    ('m::DeviceIdentity', [('imei', 'String'), ('imeisv', 'String'), ('esn', 'String'), ('meid', 'String')]),
    ('m::ActivityStatsTechSpecificInfo', [('rat', 'i32'), ('frequency_range', 'i32'), ('txm_mode_time_ms', '[i32;5]'), ('rx_mode_time_ms', 'i32')]),
    ('m::ActivityStatsInfo', [('sleep_mode_time_ms', 'i32'), ('idle_mode_time_ms', 'i32'), ('tech_specific_info', '*m::ActivityStatsTechSpecificInfo')]),
    ('m::RadioCapability', [('session', 'i32'), ('phase', 'i32'), ('raf', 'i32'), ('logical_modem_uuid', 'String'), ('status', 'i32')]),
    ('m::SlotPortMapping', [('physical_slot_id', 'i32'), ('port_id', 'i32')]),
    ('m::AppStatus', [('app_type', 'i32'), ('app_state', 'i32'), ('perso_substate', 'i32'), ('aid_ptr', 'String'), ('app_label_ptr', 'String'), ('pin1_replaced', 'bool'), ('pin1', 'i32'), ('pin2', 'i32')]),
    ('m::CardStatus', [('card_state', 'm::CardState'), ('universal_pin_state', 'i32'), ('gsm_umts_subscription_app_index', 'i32'), ('cdma_subscription_app_index', 'i32'), ('ims_subscription_app_index', 'i32'), ('applications', '*m::AppStatus'), ('atr', 'String'), ('iccid', 'String'), ('eid', 'String'), ('slot_map', 'm::SlotPortMapping'), ('supported_mep_mode', 'i32')]),
    ('m::SimPortInfo', [('icc_id', 'String'), ('logical_slot_id', 'i32'), ('port_active', 'bool')]),
    ('m::SimSlotStatus', [('card_state', 'm::CardState'), ('atr', 'String'), ('eid', 'String'), ('port_info', '*m::SimPortInfo'), ('supported_mep_mode', 'i32')]),
    ('m::PhoneCapability', [('max_active_data', 'i8'), ('max_active_internet_data', 'i8'), ('is_internet_lingering_supported', 'bool'), ('logical_modem_ids', '*i8'), ('max_active_voice', 'i8')]),
    ('m::GsmBroadcastSmsConfigInfo', [('from_service_id', 'i32'), ('to_service_id', 'i32'), ('from_code_scheme', 'i32'), ('to_code_scheme', 'i32'), ('selected', 'bool')]),
    ('m::UusInfo', [('uus_type', 'i32'), ('uus_dcs', 'i32'), ('uus_data', 'String')]),
    ('m::Call', [('state', 'i32'), ('index', 'i32'), ('toa', 'i32'), ('is_mpty', 'bool'), ('is_mt', 'bool'), ('als', 'i8'), ('is_voice', 'bool'), ('is_voice_privacy', 'bool'), ('number', 'String'), ('number_presentation', 'i32'), ('name', 'String'), ('name_presentation', 'i32'), ('uus_info', '*m::UusInfo'), ('audio_quality', 'i32'), ('forwarded_number', 'String')]),
]
NETWORK_REQUESTS = [('GetVoiceRegistrationState', ''), ('GetDataRegistrationState', ''), ('GetOperator', ''), ('GetNetworkSelectionMode', ''), ('GetSignalStrength', ''), ('GetCellInfoList', ''), ('GetImsRegistrationState', ''), ('SetIndicationFilter', 'u32'), ('SetSignalStrengthReportingCriteria', '*n::SignalCriteria'), ('SetLinkCapacityReportingCriteria', 'n::LinkCriteria')]
MODEM_REQUESTS = [('SetRadioPower', '{power}'), ('GetBasebandVersion', ''), ('GetImei', ''), ('GetDeviceIdentity', ''), ('GetModemActivityInfo', ''), ('GetModemStackStatus', ''), ('EnableModem', 'bool'), ('SendDeviceState', '{device}'), ('GetRadioCapability', ''), ('GetIccCardStatus', ''), ('GetUiccApplicationsEnablement', ''), ('AreUiccApplicationsEnabled', ''), ('EnableUiccApplications', 'bool'), ('SetCarrierInfoForImsiEncryption', ''), ('GetHalDeviceCapabilities', ''), ('GetSimSlotsStatus', ''), ('GetPhoneCapability', ''), ('GetNumOfLiveModems', ''), ('SetNumOfLiveModems', 'i8'), ('SetPreferredDataModem', 'i8'), ('GetCurrentCalls', ''), ('SetTtyMode', 'i32'), ('SetGsmBroadcastConfig', '*m::GsmBroadcastSmsConfigInfo'), ('SetDataProfile', '{count}'), ('SetNullCipherAndIntegrityEnabled', 'bool'), ('SetSecurityAlgorithmsUpdatedEnabled', 'bool'), ('SetCellularIdentifierDisclosedEnabled', 'bool'), ('IsSatelliteEnabledForCarrier', ''), ('GetBarringInfo', ''), ('GetVoiceRadioTechnology', '')]
NETWORK_VALUES = [('Void', ''), ('Registration', 'n::RegStateResult'), ('Operator', 'n::OperatorInfo'), ('Selection', 'n::SelectionMode'), ('Signal', 'n::SignalStrength'), ('Cells', '*n::CellInfo')]
MODEM_VALUES = [('Void', ''), ('Baseband', 'String'), ('Imei', 'm::ImeiInfo'), ('Identity', 'm::DeviceIdentity'), ('Activity', 'm::ActivityStatsInfo'), ('Capability', 'm::RadioCapability'), ('Card', 'm::CardStatus'), ('Slots', '*m::SimSlotStatus'), ('Phone', 'm::PhoneCapability'), ('ReducedFeatureSet1', 'bool'), ('CurrentCalls', '*m::Call'), ('VoiceRadioTechnology', 'i32')]


def snake(name):
    return re.sub('_+', '_', re.sub(r'(?<!^)(?=[A-Z])', '_', name)).lower()


def wire(t):
    if t in SCALARS or t == '[i32;5]':
        return t
    if t == 'bool':
        return 'u8'
    if t in ENUMS:
        return 'i32'
    return 'Mr' + t.split('::')[-1]


STRUCTS = [('MrString', [('data', '*const u8'), ('len', 'u32')]), ('MrCellIdentity', [('kind', 'u32'), ('area', 'u32'), ('cell', 'u32'), ('plmn', 'MrString')])]


def fields_for(fields):
    result = []
    for name, t in fields:
        if t.startswith('?'):
            result.extend([(name + '_present', 'u8'), (name, wire(t[1:]))])
        elif t.startswith('*'):
            result.extend([(name + '_data', '*const ' + wire(t[1:])), (name + '_len', 'u32')])
        else:
            result.append((name, wire(t)))
    return result


for model, fields in MODELS:
    STRUCTS.append((wire(model), fields_for(fields)))
request_fields = [('kind', 'u32'), ('boolean', 'u8'), ('mask', 'u32'), ('integer', 'i32'), ('small_integer', 'i8'), ('profile_count', 'u64'), ('power_on', 'u8'), ('for_emergency_call', 'u8'), ('preferred_for_emergency_call', 'u8'), ('device_state_type', 'i32'), ('signal_criteria_data', '*const MrSignalCriteria'), ('signal_criteria_len', 'u32'), ('link_criteria', 'MrLinkCriteria'), ('broadcast_config_data', '*const MrGsmBroadcastSmsConfigInfo'), ('broadcast_config_len', 'u32')]
STRUCTS.append(('MrRequest', request_fields))
value_fields = [('kind', 'u32'), ('boolean', 'u8'), ('integer', 'i32'), ('text', 'MrString')]
for field, model in [('registration', 'n::RegStateResult'), ('operator_info', 'n::OperatorInfo'), ('signal', 'n::SignalStrength'), ('imei', 'm::ImeiInfo'), ('identity', 'm::DeviceIdentity'), ('activity', 'm::ActivityStatsInfo'), ('capability', 'm::RadioCapability'), ('card', 'm::CardStatus'), ('phone', 'm::PhoneCapability')]:
    value_fields.append((field, wire(model)))
value_fields.extend([('selection', 'i32'), ('cells_data', '*const MrCellInfo'), ('cells_len', 'u32'), ('slots_data', '*const MrSimSlotStatus'), ('slots_len', 'u32'), ('calls_data', '*const MrCall'), ('calls_len', 'u32')])
STRUCTS.extend([('MrValue', value_fields), ('MrIndication', [('kind', 'u32'), ('signal', 'MrSignalStrength'), ('radio_state', 'i32'), ('generation', 'u64')]), ('MrOutput', [('kind', 'u32'), ('invocation_id', 'u64'), ('error', 'i32'), ('value_present', 'u8'), ('value', 'MrValue'), ('indication', 'MrIndication')]), ('MrLayout', [('name', 'MrString'), ('size', 'u64'), ('alignment', 'u64'), ('offsets_data', '*const u64'), ('offsets_len', 'u32')])])


def cname(name):
    return 'mr_' + snake(name[2:])


def ctype(t, name):
    if t == '[i32;5]':
        return f'int32_t {name}[5]'
    if t.startswith('*const '):
        target = t[7:]
        return f'const {SCALARS.get(target, cname(target))} *{name}'
    return f'{SCALARS.get(t, cname(t))} {name}'


def encode(t, expr):
    return f'{expr}.encode(arena)'


def decode(t, expr):
    return f'<{t} as Codec>::decode({expr}, reader)?'


rust = [SPDX, 'use std::mem::{align_of, offset_of, size_of};\n']
header = [SPDX, '#ifndef MINDONE_RADIO_H\n#define MINDONE_RADIO_H\n#include <stdint.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n']
for name, fields in STRUCTS:
    rust.append(f'#[repr(C)]\n#[derive(Clone, Copy, Debug, Default)]\npub struct {name} {{\n')
    header.append(f'typedef struct {cname(name)} {{\n')
    for field, t in fields:
        rust.append(f'    pub {field}: {t},\n')
        header.append(f'    {ctype(t, field)};\n')
    rust.append('}\n')
    header.append(f'}} {cname(name)};\n')
header.append('typedef uint64_t mr_runtime;\ntypedef uint64_t mr_batch;\n')
constants = [('MR_OK', 0), ('MR_INVALID_ARGUMENT', 1), ('MR_FULL', 2), ('MR_STOPPED', 3), ('MR_ID_EXHAUSTED', 4), ('MR_IO_ERROR', 5), ('MR_PANIC', 6), ('MR_INVALID_HANDLE', 7), ('MR_TIMEOUT', 8)]
for domain, requests, base in [('NETWORK', NETWORK_REQUESTS, 1), ('MODEM', MODEM_REQUESTS, 101)]:
    constants.extend((f'MR_REQUEST_{domain}_{snake(name).upper()}', i) for i, (name, _) in enumerate(requests, base))
for domain, values, base in [('NETWORK', NETWORK_VALUES, 1), ('MODEM', MODEM_VALUES, 101)]:
    constants.extend((f'MR_VALUE_{domain}_{snake(name).upper()}', i) for i, (name, _) in enumerate(values, base))
constants.extend([('MR_OUTPUT_RESPONSE', 1), ('MR_OUTPUT_INDICATION', 2), ('MR_INDICATION_NETWORK_STATE_CHANGED', 1), ('MR_INDICATION_CURRENT_SIGNAL_STRENGTH', 2), ('MR_INDICATION_RADIO_STATE_CHANGED', 3), ('MR_INDICATION_SIM_STATUS_CHANGED', 4), ('MR_INDICATION_MODEM_RESET', 5)])
for t, entries in ENUMS.items():
    constants.extend((f'MR_{snake(t.split("::")[-1]).upper()}_{snake(name).upper()}', value) for name, value in entries)
constants.extend([('MR_CELL_GSM', 1), ('MR_CELL_WCDMA', 2), ('MR_CELL_LTE', 3), ('MR_ABI_VERSION', 2), ('MR_MAX_INPUT_BYTES', 4194304), ('MR_MAX_ARRAY_ELEMENTS', 65536)])
for name, value in constants:
    header.append(f'#define {name} {value}\n')
    rust.append(f'pub const {name}: i32 = {value};\n')
header.append('#define MR_WAIT_FOREVER UINT32_MAX\n')
rust.append('pub const MR_WAIT_FOREVER: u32 = u32::MAX;\n')
rust.append('pub(crate) fn layouts() -> Vec<MrLayout> { vec![\n')
for name, fields in STRUCTS:
    offsets = ', '.join(f'offset_of!({name}, {f}) as u64' for f, _ in fields)
    label = cname(name)
    rust.append(f'MrLayout {{ name: MrString {{ data: b"{label}".as_ptr(), len: {len(label)} }}, size: size_of::<{name}>() as u64, alignment: align_of::<{name}>() as u64, offsets_data: &[{offsets}] as *const u64, offsets_len: {len(fields)} }},\n')
rust.append('] }\n')
header.append('uint32_t mr_abi_version(void);\nint32_t mr_create(mr_runtime *out);\nint32_t mr_submit(mr_runtime runtime, uint64_t invocation_id, const mr_request *request);\nint32_t mr_next_batch(mr_runtime runtime, uint32_t timeout_ms, mr_batch *out);\nint32_t mr_batch_count(mr_batch batch, uint32_t *out);\nint32_t mr_batch_at(mr_batch batch, uint32_t index, const mr_output **out);\nint32_t mr_batch_free(mr_batch *batch);\nint32_t mr_stop(mr_runtime runtime);\nint32_t mr_destroy(mr_runtime *runtime);\nuint32_t mr_layout_count(void);\nint32_t mr_layout_at(uint32_t index, mr_layout *out);\n#ifdef __cplusplus\n}\n#endif\n#endif\n')
conversion = [SPDX, 'use crate::{arena::Arena, wire::*};\nuse mindone_radio_network::types as n;\nuse mindone_radio_modem::types as m;\nuse mindone_radio_state::model as s;\nuse mindone_radio_runtime::{core::{Request, Value, Indication}, Delivery};\n\npub(crate) type Result<T> = std::result::Result<T, i32>;\npub(crate) trait Reader {\n    fn array<T: Copy>(&self, data: *const T, len: u32) -> Result<Vec<T>>;\n}\npub(crate) trait Codec: Sized {\n    type Wire: Copy + Default;\n    fn encode(&self, arena: &mut Arena) -> Self::Wire;\n    fn decode(wire: Self::Wire, reader: &impl Reader) -> Result<Self>;\n}\n']
for t in SCALARS:
    conversion.append(f'impl Codec for {t} {{ type Wire = {t}; fn encode(&self, _: &mut Arena) -> {t} {{ *self }} fn decode(wire: {t}, _: &impl Reader) -> Result<Self> {{ Ok(wire) }} }}\n')
conversion.append('impl Codec for [i32; 5] { type Wire = Self; fn encode(&self, _: &mut Arena) -> Self { *self } fn decode(wire: Self, _: &impl Reader) -> Result<Self> { Ok(wire) } }\nimpl Codec for bool { type Wire = u8; fn encode(&self, _: &mut Arena) -> u8 { u8::from(*self) } fn decode(wire: u8, _: &impl Reader) -> Result<Self> { match wire { 0 => Ok(false), 1 => Ok(true), _ => Err(MR_INVALID_ARGUMENT) } } }\nimpl Codec for String { type Wire = MrString; fn encode(&self, arena: &mut Arena) -> MrString { let (data, len) = arena.store(self.as_bytes().to_vec()); MrString { data, len } } fn decode(wire: MrString, reader: &impl Reader) -> Result<Self> { String::from_utf8(reader.array(wire.data, wire.len)?).map_err(|_| MR_INVALID_ARGUMENT) } }\nfn array_encode<T: Codec>(values: &[T], arena: &mut Arena) -> (*const T::Wire, u32) where T::Wire: \'static { let data = values.iter().map(|v| v.encode(arena)).collect(); arena.store(data) }\nfn array_decode<T: Codec>(data: *const T::Wire, len: u32, reader: &impl Reader) -> Result<Vec<T>> { reader.array(data, len)?.into_iter().map(|v| T::decode(v, reader)).collect() }\n')
for t, entries in ENUMS.items():
    arms = ', '.join(f'{t}::{name} => {value}' for name, value in entries)
    reverse = ', '.join(f'{value} => Ok({t}::{name})' for name, value in entries)
    conversion.append(f'impl Codec for {t} {{ type Wire = i32; fn encode(&self, _: &mut Arena) -> i32 {{ match self {{ {arms} }} }} fn decode(wire: i32, _: &impl Reader) -> Result<Self> {{ match wire {{ {reverse}, _ => Err(MR_INVALID_ARGUMENT) }} }} }}\n')
conversion.append('impl Codec for n::CellIdentity { type Wire = MrCellIdentity; fn encode(&self, arena: &mut Arena) -> MrCellIdentity { let (kind, area, cell, plmn) = match self { Self::Gsm {lac,cid,plmn} => (1,*lac,*cid,plmn), Self::Wcdma {lac,cid,plmn} => (2,*lac,*cid,plmn), Self::Lte {tac,ci,plmn} => (3,*tac,*ci,plmn) }; MrCellIdentity {kind,area,cell,plmn:plmn.encode(arena)} } fn decode(w: MrCellIdentity, reader: &impl Reader) -> Result<Self> { if !(1..=3).contains(&w.kind) { return Err(MR_INVALID_ARGUMENT); } let plmn = String::decode(w.plmn,reader)?; Ok(match w.kind { 1 => Self::Gsm {lac:w.area,cid:w.cell,plmn}, 2 => Self::Wcdma {lac:w.area,cid:w.cell,plmn}, _ => Self::Lte {tac:w.area,ci:w.cell,plmn} }) } }\n')
for model, fields in MODELS:
    encode_lines, encode_fields, decode_fields = [], [], []
    for field, t in fields:
        if t.startswith('*'):
            inner = t[1:]
            encode_lines.append(f'let ({field}_data, {field}_len) = array_encode(&self.{field}, arena);')
            encode_fields.extend([field + '_data', field + '_len'])
            decode_fields.append(f'{field}: array_decode::<{inner}>(w.{field}_data, w.{field}_len, reader)?')
        elif t.startswith('?'):
            inner = t[1:]
            encode_fields.extend([f'{field}_present: u8::from(self.{field}.is_some())', f'{field}: self.{field}.as_ref().map(|v| v.encode(arena)).unwrap_or_default()'])
            decode_fields.append(f'{field}: match w.{field}_present {{ 0 => None, 1 => Some({decode(inner, "w." + field)}), _ => return Err(MR_INVALID_ARGUMENT) }}')
        else:
            encode_fields.append(f'{field}: {encode(t, "self." + field)}')
            decode_fields.append(f'{field}: {decode(t, "w." + field)}')
    conversion.append(f'impl Codec for {model} {{ type Wire = {wire(model)}; fn encode(&self, arena: &mut Arena) -> Self::Wire {{ {" ".join(encode_lines)} {wire(model)} {{ {", ".join(encode_fields)} }} }} fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {{ Ok(Self {{ {", ".join(decode_fields)} }}) }} }}\n')


def payload_field(t, variant, category):
    if category == 'request':
        return {'bool': 'boolean', 'u32': 'mask', 'i32': 'integer', 'i8': 'small_integer', '*n::SignalCriteria': 'signal_criteria', 'n::LinkCriteria': 'link_criteria', '*m::GsmBroadcastSmsConfigInfo': 'broadcast_config'}[t]
    return {'bool': 'boolean', 'i32': 'integer', 'String': 'text', 'n::RegStateResult': 'registration', 'n::OperatorInfo': 'operator_info', 'n::SelectionMode': 'selection', 'n::SignalStrength': 'signal', '*n::CellInfo': 'cells', 'm::ImeiInfo': 'imei', 'm::DeviceIdentity': 'identity', 'm::ActivityStatsInfo': 'activity', 'm::RadioCapability': 'capability', 'm::CardStatus': 'card', '*m::SimSlotStatus': 'slots', 'm::PhoneCapability': 'phone', '*m::Call': 'calls'}[t]


for category, rust_type, sets in [('request', 'Request', [('Network', 'n', NETWORK_REQUESTS, 1), ('Modem', 'm', MODEM_REQUESTS, 101)]), ('value', 'Value', [('Network', 'n', NETWORK_VALUES, 1), ('Modem', 'm', MODEM_VALUES, 101)])]:
    enc, dec = [], []
    wt = 'Mr' + rust_type
    for domain, module, entries, base in sets:
        for index, (variant, t) in enumerate(entries, base):
            prefix = f'{rust_type}::{domain}({module}::{rust_type}::{variant}'
            if not t:
                enc.append(f'{prefix}) => {wt} {{ kind: {index}, ..Default::default() }}')
                dec.append(f'{index} => {prefix})')
            elif t == '{power}':
                enc.append(f'{prefix} {{on,for_emergency_call,preferred_for_emergency_call}}) => {wt} {{ kind:{index}, power_on:on.encode(arena), for_emergency_call:for_emergency_call.encode(arena), preferred_for_emergency_call:preferred_for_emergency_call.encode(arena), ..Default::default() }}')
                dec.append(f'{index} => {prefix} {{on:bool::decode(w.power_on,reader)?,for_emergency_call:bool::decode(w.for_emergency_call,reader)?,preferred_for_emergency_call:bool::decode(w.preferred_for_emergency_call,reader)?}})')
            elif t == '{device}':
                enc.append(f'{prefix}(kind,state)) => {wt} {{kind:{index},device_state_type:kind.encode(arena),boolean:state.encode(arena),..Default::default()}}')
                dec.append(f'{index} => {prefix}(m::DeviceStateType::decode(w.device_state_type,reader)?,bool::decode(w.boolean,reader)?))')
            elif t == '{count}':
                enc.append(f'{prefix} {{profile_count}}) => {wt} {{kind:{index},profile_count:u64::try_from(*profile_count).expect("profile count"),..Default::default()}}')
                dec.append(f'{index} => {prefix} {{profile_count:usize::try_from(w.profile_count).map_err(|_| MR_INVALID_ARGUMENT)?}})')
            else:
                field = payload_field(t, variant, category)
                if t.startswith('*'):
                    enc.append(f'{prefix}(v)) => {{ let ({field}_data,{field}_len) = array_encode(v,arena); {wt} {{ kind:{index},{field}_data,{field}_len,..Default::default() }} }}')
                    dec.append(f'{index} => {prefix}(array_decode::<{t[1:]}>(w.{field}_data,w.{field}_len,reader)?))')
                else:
                    enc.append(f'{prefix}(v)) => {wt} {{kind:{index},{field}:v.encode(arena),..Default::default()}}')
                    dec.append(f'{index} => {prefix}({decode(t, "w." + field)}))')
    conversion.append(f'impl Codec for {rust_type} {{ type Wire = {wt}; fn encode(&self, arena: &mut Arena) -> Self::Wire {{ match self {{ {",".join(enc)} }} }} fn decode(w: Self::Wire, reader: &impl Reader) -> Result<Self> {{ Ok(match w.kind {{ {",".join(dec)}, _ => return Err(MR_INVALID_ARGUMENT) }}) }} }}\n')
conversion.append('impl Codec for Indication { type Wire = MrIndication; fn encode(&self, arena: &mut Arena) -> MrIndication { match self { Self::NetworkStateChanged => MrIndication {kind:1,..Default::default()}, Self::CurrentSignalStrength(v) => MrIndication {kind:2,signal:v.encode(arena),..Default::default()}, Self::RadioStateChanged(v) => MrIndication {kind:3,radio_state:v.encode(arena),..Default::default()}, Self::SimStatusChanged => MrIndication {kind:4,..Default::default()}, Self::ModemReset {generation} => MrIndication {kind:5,generation:*generation,..Default::default()} } } fn decode(w: MrIndication,reader:&impl Reader) -> Result<Self> { Ok(match w.kind { 1=>Self::NetworkStateChanged, 2=>Self::CurrentSignalStrength(n::SignalStrength::decode(w.signal,reader)?), 3=>Self::RadioStateChanged(s::Radio::decode(w.radio_state,reader)?), 4=>Self::SimStatusChanged, 5=>Self::ModemReset {generation:w.generation}, _=>return Err(MR_INVALID_ARGUMENT) }) } }\nimpl Codec for Delivery { type Wire = MrOutput; fn encode(&self,arena:&mut Arena)->MrOutput { match self { Self::Response {invocation,error,value} => MrOutput {kind:1,invocation_id:*invocation,error:error.encode(arena),value_present:u8::from(value.is_some()),value:value.as_ref().map(|v|v.encode(arena)).unwrap_or_default(),..Default::default()}, Self::Indication(v)=>MrOutput {kind:2,indication:v.encode(arena),..Default::default()} } } fn decode(w:MrOutput,reader:&impl Reader)->Result<Self> { Ok(match w.kind { 1=>Self::Response {invocation:w.invocation_id,error:m::RadioError::decode(w.error,reader)?,value:match w.value_present {0=>None,1=>Some(Value::decode(w.value,reader)?),_=>return Err(MR_INVALID_ARGUMENT)}},2=>Self::Indication(Indication::decode(w.indication,reader)?),_=>return Err(MR_INVALID_ARGUMENT) }) } }\n')


def sample(t, present=True, seed=1):
    if t.startswith('?'):
        return f'Some({sample(t[1:], present, seed)})' if present else 'None'
    if t.startswith('*'):
        return f'vec![{sample(t[1:], present, seed)}, {sample(t[1:], present, seed + 1)}]'
    if t in ENUMS:
        return t + '::' + ENUMS[t][seed % len(ENUMS[t])][0]
    if t == 'String':
        return f'"field{seed}\\0日本語".to_owned()'
    if t == 'bool':
        return 'true' if seed % 2 else 'false'
    if t == '[i32;5]':
        return '[1,-2,3,-4,5]'
    if t in SCALARS:
        value = seed % 97 + 1
        return str(-value if t.startswith('i') and seed % 2 else value)
    if t == 'n::CellIdentity':
        return 'n::CellIdentity::Lte {tac:23,ci:42,plmn:"44010".into()}'
    fields = dict(MODELS)[t]
    return t + ' {' + ','.join(f'{f}:{sample(ft, present, seed * 31 + index)}' for index, (f, ft) in enumerate(fields)) + '}'


tests = [SPDX, 'use super::*;\nuse crate::convert::Codec;\nuse mindone_radio_network::types as n;\nuse mindone_radio_modem::types as m;\nuse mindone_radio_state::model as s;\nuse mindone_radio_runtime::{core::{Request,Value,Indication},Delivery};\nfn round_trip<T: Codec + std::fmt::Debug + PartialEq>(value:T) { let mut arena=Arena::default(); let wire=value.encode(&mut arena); let decoded=T::decode(wire,&Input::default()).unwrap(); drop(arena); assert_eq!(value,decoded); }\n']
for t, entries in ENUMS.items():
    for name, number in entries:
        if t == 'm::RadioError':
            tests.append(f'#[test] fn error_number_{snake(name)}() {{ let mut arena=Arena::default(); assert_eq!({t}::{name}.encode(&mut arena),{number}); assert_eq!({t}::{name}.encode(&mut arena),{t}::{name} as i32); }}\n')
        tests.append(f'#[test] fn enum_{snake(t.replace("::", "_"))}_{snake(name)}() {{round_trip({t}::{name});}}\n')
for model, _ in MODELS:
    for present in [True, False]:
        tests.append(f'#[test] fn model_{snake(model.replace("::", "_"))}_{int(present)}() {{round_trip({sample(model,present)});}}\n')
for category, rt, sets in [('request', 'Request', [('Network', 'n', NETWORK_REQUESTS), ('Modem', 'm', MODEM_REQUESTS)]), ('value', 'Value', [('Network', 'n', NETWORK_VALUES), ('Modem', 'm', MODEM_VALUES)])]:
    for domain, module, entries in sets:
        for variant, t in entries:
            payload = ''
            if t == '{power}':
                payload = '{on:true,for_emergency_call:false,preferred_for_emergency_call:true}'
            elif t == '{device}':
                payload = '(m::DeviceStateType::ChargingState,true)'
            elif t == '{count}':
                payload = '{profile_count:42}'
            elif t:
                payload = '(' + sample(t) + ')'
            expr = f'{rt}::{domain}({module}::{rt}::{variant}{payload})'
            tests.append(f'#[test] fn {category}_{snake(domain)}_{snake(variant)}() {{ round_trip({expr}); }}\n')
for i, expr in enumerate(['Indication::NetworkStateChanged', f'Indication::CurrentSignalStrength({sample("n::SignalStrength")})', 'Indication::RadioStateChanged(s::Radio::Unavailable)', 'Indication::RadioStateChanged(s::Radio::Off)', 'Indication::RadioStateChanged(s::Radio::On)', 'Indication::SimStatusChanged', 'Indication::ModemReset {generation:u64::MAX}']):
    tests.append(f'#[test] fn indication_{i}() {{ round_trip({expr}); }}\n')
for i, expr in enumerate(['n::CellIdentity::Gsm {lac:1,cid:2,plmn:"".into()}', 'n::CellIdentity::Wcdma {lac:3,cid:4,plmn:"44010".into()}', sample('n::CellIdentity')]):
    tests.append(f'#[test] fn cell_identity_{i}() {{ round_trip({expr}); }}\n')
tests.append('#[test] fn delivery_round_trips() { round_trip(Delivery::Response { invocation:u64::MAX,error:m::RadioError::OemError1,value:None }); round_trip(Delivery::Response { invocation:7,error:m::RadioError::None,value:Some(Value::Network(n::Value::Void)) }); round_trip(Delivery::Indication(Indication::NetworkStateChanged)); }\n')
c = [SPDX, '#include "../mindone_radio.h"\n#include <assert.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\nint main(void) {\nassert(mr_abi_version() == MR_ABI_VERSION);\n']
c.append(f'assert(mr_layout_count() == {len(STRUCTS)});\n')
for index, (name, fields) in enumerate(STRUCTS):
    ct = cname(name)
    c.append(f'{{ mr_layout layout; assert(mr_layout_at({index}, &layout) == MR_OK); assert(layout.name.len == strlen("{ct}")); assert(memcmp(layout.name.data,"{ct}",layout.name.len)==0); assert(layout.size == sizeof({ct})); assert(layout.alignment == _Alignof({ct})); assert(layout.offsets_len == {len(fields)});\n')
    for i, (f, _) in enumerate(fields):
        c.append(f'assert(layout.offsets_data[{i}] == offsetof({ct}, {f}));\n')
    c.append('}\n')
c.append('mr_batch batch = 0;\nmr_runtime runtime = 0;\nuint32_t count = 0;\nconst mr_output *output = NULL;\nassert(mr_create(NULL) == MR_INVALID_ARGUMENT);\nassert(mr_next_batch(0,0,&batch) == MR_INVALID_HANDLE);\nassert(mr_batch_count(0,&count) == MR_INVALID_HANDLE);\nassert(mr_batch_at(0,0,&output) == MR_INVALID_HANDLE);\nassert(mr_stop(0) == MR_INVALID_HANDLE);\nassert(mr_batch_free(&batch) == MR_INVALID_HANDLE);\nassert(mr_destroy(&runtime) == MR_INVALID_HANDLE);\nassert(mr_submit(0,1,NULL) == MR_INVALID_ARGUMENT);\nassert(mr_layout_at(UINT32_MAX,NULL) == MR_INVALID_ARGUMENT);\nprintf("C layouts: %u structs verified\\n",mr_layout_count());\nreturn 0;\n}\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    outputs = {'wire.rs': ''.join(rust), 'mindone_radio.h': ''.join(header), 'convert.rs': ''.join(conversion), 'tests/generated.rs': ''.join(tests), 'tests/layout.c': ''.join(c)}
    for name, content in outputs.items():
        path = ROOT / name
        if name.endswith('.rs'):
            formatter = os.environ.get('RUSTFMT') or shutil.which('rustfmt') or '/opt/homebrew/bin/rustfmt'
            content = subprocess.run([formatter, '--edition=2021', '--emit=stdout'], input=content, text=True, capture_output=True, check=True).stdout
        if args.check:
            if path.read_text() != content:
                raise SystemExit(f'Generated file differs: {path}')
        else:
            path.write_text(content)


if __name__ == '__main__':
    main()
