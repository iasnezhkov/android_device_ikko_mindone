/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::parse::{Csq, Ecsq, Extensions, Family, RegistrationReport};
use std::collections::BTreeMap;

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub enum Knowledge<T> { #[default] Unknown, Known(T) }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Domain { Cs, Ps }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Radio { Unavailable, Off, On }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Registration {
    pub state: u32,
    pub rat_mtk_unverified: Option<u32>,
    pub area: Option<u32>,
    pub cell: Option<u64>,
    pub plmn: Option<String>,
    pub reject_cause: Option<i32>,
    pub emergency_status_unverified: Option<i32>,
}

impl Registration {
    pub fn from_report(report: &RegistrationReport) -> Option<(Domain, Self)> {
        let (domain, reject_cause, emergency_status_unverified) = match (&report.family, &report.extensions) {
            (Family::Ereg, Extensions::Cs { reject_cause, emergency_status, .. }) => (Domain::Cs, *reject_cause, *emergency_status),
            (Family::Egreg, Extensions::Ps { reject_cause, .. }) => (Domain::Ps, *reject_cause, None),
            _ => return None,
        };
        Some((domain, Self {
            state: report.state, rat_mtk_unverified: report.rat, area: report.area.filter(|area| *area != 0xffff),
            cell: report.cell.filter(|cell| *cell != 0xfffffff), plmn: None, reject_cause, emergency_status_unverified,
        }))
    }
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum SignalRat { GsmCsq, MtkUnverified(i32) }

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Signal {
    Gsm(Csq),
    Lte { rsrp: Option<i32>, rsrq: Option<i32>, rssnr: Option<i32> },
    QuarterUnitsUnverified { ber: Option<i32>, rssi: Option<i32>, quality: Option<i32>, power: Option<i32>, snr: Option<i32>, csi: Option<[Option<i32>; 3]> },
}

fn measurement(value: i32) -> Option<i32> {
    (value != 32767 && value != i32::MAX).then_some(value)
}

impl Signal {
    pub fn from_ecsq(report: &Ecsq) -> (SignalRat, Self) {
        let v = report.values;
        if matches!(v[7], 1 | 2) {
            let rssi = measurement(v[2]).map(|raw| (((i64::from(raw) >> 2) + 113) >> 1).clamp(0, 31) as u8);
            let ber = u8::try_from(v[1]).ok().filter(|n| *n <= 7);
            return (SignalRat::GsmCsq, Self::Gsm(Csq { rssi, ber }));
        }
        let rat = SignalRat::MtkUnverified(v[7]);
        let signal = if matches!(v[7], 4096 | 8192) {
            Self::Lte {
                rsrp: measurement(v[6]).map(|n| (-(i64::from(n) >> 2)).clamp(44, 140) as i32),
                rsrq: measurement(v[5]).map(|n| (-(i64::from(n) >> 2)).clamp(3, 20) as i32),
                rssnr: measurement(v[8]).map(|n| ((10 * i64::from(n)) >> 2).clamp(-200, 300) as i32),
            }
        } else {
            Self::QuarterUnitsUnverified {
                ber: (v[1] != 99).then_some(v[1]), rssi: measurement(v[2]),
                quality: measurement(v[5]), power: measurement(v[6]), snr: measurement(v[8]),
                csi: report.nr_csi_unverified.map(|v| v.map(measurement)),
            }
        };
        (rat, signal)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Source { Query, Urc, Transport, PowerResponse }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Observation<T> {
    pub value: T,
    pub observed_ms: u64,
    pub source: Source,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Update {
    Registration(Domain, Registration),
    Signal(SignalRat, Signal),
    Radio(Radio),
    Sim(crate::parse::SimReport),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Event {
    pub generation: u64,
    pub observed_ms: u64,
    pub source: Source,
    pub update: Update,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Change { Registration(Domain), Signal(SignalRat), Radio, Sim }

pub struct Model {
    pub generation: u64,
    pub sim: Observation<Sim>,
    pub cs: Observation<Knowledge<Registration>>,
    pub ps: Observation<Knowledge<Registration>>,
    pub signals: BTreeMap<SignalRat, Observation<Signal>>,
    pub radio: Observation<Radio>,
}

fn observe<T: PartialEq>(slot: &mut Observation<T>, value: T, now: u64, source: Source) -> bool {
    if now < slot.observed_ms { return false; }
    let changed = slot.value != value;
    *slot = Observation { value, observed_ms: now, source };
    changed
}

impl Model {
    pub fn new(generation: u64) -> Self {
        let registration = Observation { value: Knowledge::Unknown, observed_ms: 0, source: Source::Transport };
        Self {
            generation, sim: Observation { value: Sim::default(), observed_ms: 0, source: Source::Transport }, cs: registration.clone(), ps: registration, signals: BTreeMap::new(),
            radio: Observation { value: Radio::Unavailable, observed_ms: 0, source: Source::Transport },
        }
    }

    pub fn apply(&mut self, event: Event) -> Option<Change> {
        if event.generation != self.generation { return None; }
        match event.update {
            Update::Sim(report) => {
                if event.observed_ms < self.sim.observed_ms { return None; }
                let before = self.sim.value.semantic_key();
                self.sim.value.reduce(report);
                self.sim.observed_ms = event.observed_ms;
                self.sim.source = event.source;
                (before != self.sim.value.semantic_key()).then_some(Change::Sim)
            }
            Update::Registration(domain, registration) => {
                let slot = match domain { Domain::Cs => &mut self.cs, Domain::Ps => &mut self.ps };
                observe(slot, Knowledge::Known(registration), event.observed_ms, event.source).then_some(Change::Registration(domain))
            }
            Update::Radio(radio) => observe(&mut self.radio, radio, event.observed_ms, event.source).then_some(Change::Radio),
            Update::Signal(rat, signal) => {
                let changed = if let Some(slot) = self.signals.get_mut(&rat) {
                    observe(slot, signal, event.observed_ms, event.source)
                } else {
                    self.signals.insert(rat, Observation { value: signal, observed_ms: event.observed_ms, source: event.source });
                    true
                };
                changed.then_some(Change::Signal(rat))
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum SimCard { #[default] Unknown, Absent, Present, Error }

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Sim {
    pub card: SimCard,
    pub app: Knowledge<crate::parse::SimApp>,
    pub applications_enabled: Knowledge<bool>,
    pub esims_status_unverified: Option<u32>,
    pub eusim_unverified: Vec<i32>,
    pub vendor_status_unverified: Option<String>,
}

impl Sim {
    fn semantic_key(&self) -> (SimCard, Knowledge<crate::parse::SimApp>, Knowledge<bool>) {
        (self.card, self.app.clone(), self.applications_enabled.clone())
    }

    fn reduce(&mut self, report: crate::parse::SimReport) {
        use crate::parse::{SimApp, SimReport};
        match report {
            SimReport::Absent => { self.card = SimCard::Absent; self.app = Knowledge::Unknown; }
            SimReport::Failure => { self.card = SimCard::Error; self.app = Knowledge::Unknown; }
            SimReport::Busy => (),
            SimReport::Cpin(SimApp::VendorUnverified(value)) => self.vendor_status_unverified = Some(value),
            SimReport::Cpin(app) => {
                self.card = SimCard::Present;
                self.app = Knowledge::Known(app);
                self.vendor_status_unverified = None;
            }
            SimReport::Esims { state, status, .. } => {
                self.esims_status_unverified = Some(status);
                match (state, status) {
                    (0, 0) => { self.card = SimCard::Absent; self.app = Knowledge::Unknown; }
                    (0, 28) => self.applications_enabled = Knowledge::Known(false),
                    (1, 29) => self.applications_enabled = Knowledge::Known(true),
                    _ => (),
                }
            }
            SimReport::EusimUnverified(values) => self.eusim_unverified = values,
        }
    }
}
