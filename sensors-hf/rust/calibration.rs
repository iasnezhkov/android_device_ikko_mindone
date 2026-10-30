/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
use crate::{Error, CALIBRATION_BYTES};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Region {
    pub offset: usize,
    pub length: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Layout {
    pub length: usize,
    pub bias: Region,
    pub cali: Region,
    pub temp: Region,
}

pub fn layout(sensor_type: u8) -> Result<Layout, Error> {
    let (length, bias, cali, temp) = match sensor_type {
        1 => (24, (0, 12), (12, 12), (0, 0)),
        2 => (36, (0, 12), (12, 24), (0, 0)),
        4 => (48, (0, 12), (12, 12), (24, 24)),
        5 => (4, (0, 0), (0, 4), (0, 0)),
        _ => return Err(Error::Sensor),
    };
    Ok(Layout {
        length,
        bias: Region { offset: bias.0, length: bias.1 },
        cali: Region { offset: cali.0, length: cali.1 },
        temp: Region { offset: temp.0, length: temp.1 },
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Payload {
    pub bytes: [u8; CALIBRATION_BYTES],
    pub layout: Layout,
    pub combined: bool,
}

fn write_region(bytes: &mut [u8; CALIBRATION_BYTES], region: Region, values: &[i32]) {
    for (raw, value) in bytes[region.offset..region.offset + region.length]
        .chunks_exact_mut(4).zip(values)
    {
        raw.copy_from_slice(&value.to_le_bytes());
    }
}

pub fn payload(sensor_type: u8, bias: &[i32], cali: &[i32], temp: &[i32]) -> Result<Payload, Error> {
    let layout = layout(sensor_type)?;
    let mut bytes = [0; CALIBRATION_BYTES];
    write_region(&mut bytes, layout.bias, bias);
    let bias_count = layout.bias.length / 4;
    let cali_count = layout.cali.length / 4;
    let combined = bias_count > 0 && cali.len() == bias_count + cali_count;
    if combined {
        write_region(&mut bytes, layout.bias, &cali[..bias_count]);
        write_region(&mut bytes, layout.cali, &cali[bias_count..]);
    } else {
        write_region(&mut bytes, layout.cali, cali);
    }
    write_region(&mut bytes, layout.temp, temp);
    Ok(Payload { bytes, layout, combined })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum Kind {
    Bias = 0,
    Cali = 1,
    Temp = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Event {
    pub words: [i32; 6],
    pub count: usize,
    pub kind: Kind,
}

pub fn event(sensor_type: u8, action: u8, words: &[i32]) -> Result<Event, Error> {
    if words.len() != 16 {
        return Err(Error::Length);
    }
    let layout = layout(sensor_type)?;
    let (kind, region) = match action {
        2 => (Kind::Bias, layout.bias),
        3 => (Kind::Cali, layout.cali),
        4 => (Kind::Temp, layout.temp),
        _ => return Err(Error::Action),
    };
    if region.length == 0 {
        return Err(Error::Action);
    }
    let count = region.length / 4;
    let mut output = [0; 6];
    output[..count].copy_from_slice(&words[..count]);
    Ok(Event { words: output, count, kind })
}
