// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use crate::proto::PtimSampleV1;
use std::time::Duration;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Sequence,
    Timestamps,
    Voltage,
    Current,
    Calibration,
    FutureSample,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Sample(PtimSampleV1);

impl TryFrom<PtimSampleV1> for Sample {
    type Error = Error;

    fn try_from(value: PtimSampleV1) -> Result<Self, Error> {
        if value.sequence == 0 {
            return Err(Error::Sequence);
        }
        if value.acquired_ns <= value.started_ns {
            return Err(Error::Timestamps);
        }
        if !(15_000..=55_000).contains(&value.voltage_01mv) {
            return Err(Error::Voltage);
        }
        if !(-300_000..=300_000).contains(&value.current_01ma) {
            return Err(Error::Current);
        }
        if !(1..=10_000).contains(&value.r_fg_value)
            || !(1..=10_000).contains(&value.car_tune_value)
        {
            return Err(Error::Calibration);
        }
        Ok(Self(value))
    }
}

impl Sample {
    pub fn wire(self) -> PtimSampleV1 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Freshness {
    Uninitialized,
    Stale(Sample),
    Valid(Sample),
}

#[derive(Debug)]
pub struct SampleCache {
    max_age: Duration,
    last: Option<Sample>,
}

impl SampleCache {
    pub fn new(max_age: Duration) -> Self {
        Self {
            max_age,
            last: None,
        }
    }

    pub fn update(&mut self, value: PtimSampleV1) -> Result<Sample, Error> {
        let sample = Sample::try_from(value)?;
        if let Some(previous) = self.last {
            if value.sequence <= previous.0.sequence {
                return Err(Error::Sequence);
            }
            if value.started_ns < previous.0.acquired_ns {
                return Err(Error::Timestamps);
            }
        }
        self.last = Some(sample);
        Ok(sample)
    }

    pub fn freshness(&self, now_boottime_ns: u64) -> Result<Freshness, Error> {
        let Some(sample) = self.last else {
            return Ok(Freshness::Uninitialized);
        };
        let age = now_boottime_ns
            .checked_sub(sample.0.acquired_ns)
            .ok_or(Error::FutureSample)?;
        if Duration::from_nanos(age) > self.max_age {
            Ok(Freshness::Stale(sample))
        } else {
            Ok(Freshness::Valid(sample))
        }
    }
}
