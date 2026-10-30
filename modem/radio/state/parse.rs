/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParseError { Prefix, Shape, Number, Range, Quotes }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Context { Solicited, Unsolicited }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Family { Ereg, Egreg, CregUnverified, CgregUnverified, CeregUnverified }

impl Family {
    fn prefix(self) -> &'static str {
        match self {
            Self::Ereg => "+EREG:", Self::Egreg => "+EGREG:",
            Self::CregUnverified => "+CREG:", Self::CgregUnverified => "+CGREG:",
            Self::CeregUnverified => "+CEREG:",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Extensions {
    Cs { emergency_status: Option<i32>, unverified: Vec<Option<i32>>, reject_cause: Option<i32> },
    Ps { bearer: Option<u32>, capability: Option<i32>, reject_cause: Option<i32>, nr_and_status_unverified: Vec<Option<i32>> },
    PlainUnverified(Vec<String>),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RegistrationReport {
    pub family: Family,
    pub mode: Option<u32>,
    pub state: u32,
    pub area: Option<u32>,
    pub cell: Option<u64>,
    pub rat: Option<u32>,
    pub extensions: Extensions,
}

fn fields<'a>(line: &'a str, prefix: &str) -> Result<Vec<&'a str>, ParseError> {
    let body = line.trim().strip_prefix(prefix).ok_or(ParseError::Prefix)?.trim();
    if body.is_empty() { return Err(ParseError::Shape); }
    body.split(',').map(|field| {
        let field = field.trim();
        if field.starts_with('"') && field.ends_with('"') && field.len() >= 2 {
            let value = &field[1..field.len() - 1];
            if value.contains('"') { return Err(ParseError::Quotes); }
            Ok(value)
        } else if field.contains('"') { Err(ParseError::Quotes) } else { Ok(field) }
    }).collect()
}

fn decimal<T: std::str::FromStr>(value: &str) -> Result<T, ParseError> {
    if value.is_empty() || !value.bytes().enumerate().all(|(i, b)| b.is_ascii_digit() || (i == 0 && b == b'-')) {
        return Err(ParseError::Number);
    }
    value.parse().map_err(|_| ParseError::Number)
}

fn optional<T>(value: Option<&&str>, parse: impl FnOnce(&str) -> Result<T, ParseError>) -> Result<Option<T>, ParseError> {
    match value { None | Some(&"") | Some(&"-1") => Ok(None), Some(value) => parse(value).map(Some) }
}

fn hex(value: &str) -> Result<u64, ParseError> {
    let value = value.strip_prefix("0x").or_else(|| value.strip_prefix("0X")).unwrap_or(value);
    if value.is_empty() || !value.bytes().all(|b| b.is_ascii_hexdigit()) { return Err(ParseError::Number); }
    u64::from_str_radix(value, 16).map_err(|_| ParseError::Range)
}

pub fn registration(line: &str, family: Family, context: Context) -> Result<RegistrationReport, ParseError> {
    let fields = fields(line, family.prefix())?;
    let offset = usize::from(context == Context::Solicited);
    if fields.len() <= offset { return Err(ParseError::Shape); }
    let mode = if offset == 1 { Some(decimal(fields[0])?) } else { None };
    let values = &fields[offset..];
    if values.len() == 2 { return Err(ParseError::Shape); }
    let state = decimal(values[0])?;
    let area = optional(values.get(1), |s| u32::try_from(hex(s)?).map_err(|_| ParseError::Range))?;
    let cell = optional(values.get(2), hex)?;
    let rat = optional(values.get(3), decimal)?;
    let tail = values.get(4..).unwrap_or_default();
    let extensions = match family {
        Family::Ereg => {
            if tail.len() > 4 { return Err(ParseError::Shape); }
            Extensions::Cs {
                emergency_status: optional(tail.first(), decimal)?,
                unverified: tail.iter().skip(1).take(2).map(|s| optional(Some(s), decimal)).collect::<Result<_, _>>()?,
                reject_cause: optional(tail.get(3), decimal)?,
            }
        }
        Family::Egreg => Extensions::Ps {
            bearer: optional(tail.first(), |s| u32::try_from(hex(s)?).map_err(|_| ParseError::Range))?,
            capability: optional(tail.get(1), decimal)?,
            reject_cause: optional(tail.get(2), decimal)?,
            nr_and_status_unverified: tail.iter().skip(3).map(|s| optional(Some(s), decimal)).collect::<Result<_, _>>()?,
        },
        _ => Extensions::PlainUnverified(tail.iter().map(|s| String::from(*s)).collect()),
    };
    Ok(RegistrationReport { family, mode, state, area, cell, rat, extensions })
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Ecsq {
    pub values: [i32; 10],
    pub nr_csi_unverified: Option<[i32; 3]>,
}

pub fn ecsq(line: &str) -> Result<Ecsq, ParseError> {
    let fields = fields(line, "+ECSQ:")?;
    if fields.len() < 10 { return Err(ParseError::Shape); }
    let mut values = [0; 10];
    for (value, field) in values.iter_mut().zip(&fields) { *value = decimal(field)?; }
    let nr_csi_unverified = if values[7] == 32768 {
        if fields.len() != 13 { return Err(ParseError::Shape); }
        Some([decimal(fields[10])?, decimal(fields[11])?, decimal(fields[12])?])
    } else {
        if fields.len() != 10 { return Err(ParseError::Shape); }
        None
    };
    Ok(Ecsq { values, nr_csi_unverified })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Eind { pub code: u32 }

pub fn eind(line: &str) -> Result<Eind, ParseError> {
    let fields = fields(line, "+EIND:")?;
    if fields.len() != 1 { return Err(ParseError::Shape); }
    Ok(Eind { code: decimal(fields[0])? })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Csq { pub rssi: Option<u8>, pub ber: Option<u8> }

pub fn csq(line: &str) -> Result<Csq, ParseError> {
    let fields = fields(line, "+CSQ:")?;
    if fields.len() != 2 { return Err(ParseError::Shape); }
    let rssi = decimal::<u8>(fields[0])?;
    let ber = decimal::<u8>(fields[1])?;
    if (rssi > 31 && rssi != 99) || (ber > 7 && ber != 99) { return Err(ParseError::Range); }
    Ok(Csq { rssi: (rssi != 99).then_some(rssi), ber: (ber != 99).then_some(ber) })
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SimApp {
    Ready, Pin, Puk, PersonalizationUnverified(String), VendorUnverified(String),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SimReport {
    Cpin(SimApp), Esims { state: u32, status: u32, extra_unverified: Vec<i32> },
    EusimUnverified(Vec<i32>), Absent, Failure, Busy,
}

pub fn sim(line: &str) -> Result<SimReport, ParseError> {
    if line.trim().starts_with("+CPIN:") {
        let values = fields(line, "+CPIN:")?;
        if values.len() != 1 { return Err(ParseError::Shape); }
        let app = match values[0] {
            "READY" => SimApp::Ready, "SIM PIN" => SimApp::Pin, "SIM PUK" => SimApp::Puk,
            value if ["PH-NET", "PH-NETSUB", "PH-SP", "PH-CORP", "PH-FSIM", "PH-NSSP", "PH-SIMC"].iter()
                .any(|prefix| value.strip_prefix(prefix).is_some_and(|tail| matches!(tail, " PIN" | " PUK"))) =>
                    SimApp::PersonalizationUnverified(value.into()),
            value if matches!(value, "EMPTY_EUICC" | "CARD_REBOOT" | "CARD_RESTRICTED" | "SIM PIN-DEFER") =>
                SimApp::VendorUnverified(value.into()),
            _ => return Err(ParseError::Range),
        };
        return Ok(SimReport::Cpin(app));
    }
    if line.trim().starts_with("+ESIMS:") {
        let values = fields(line, "+ESIMS:")?;
        if values.len() < 2 || values.len() > 8 { return Err(ParseError::Shape); }
        let state = decimal(values[0])?;
        if state > 1 { return Err(ParseError::Range); }
        return Ok(SimReport::Esims { state, status: decimal(values[1])?,
            extra_unverified: values[2..].iter().map(|v| decimal(v)).collect::<Result<_, _>>()? });
    }
    if line.trim().starts_with("+EUSIM:") {
        let values = fields(line, "+EUSIM:")?;
        if values.len() > 8 { return Err(ParseError::Shape); }
        return Ok(SimReport::EusimUnverified(values.iter().map(|v| decimal(v)).collect::<Result<_, _>>()?));
    }
    let values = fields(line, "+CME ERROR:")?;
    if values.len() != 1 { return Err(ParseError::Shape); }
    match decimal::<u32>(values[0])? {
        10 => Ok(SimReport::Absent), 13 | 15 => Ok(SimReport::Failure), 14 => Ok(SimReport::Busy),
        _ => Err(ParseError::Range),
    }
}
