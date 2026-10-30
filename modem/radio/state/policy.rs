/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

pub const SIGNAL_STRENGTH: u32 = 1;
pub const FULL_NETWORK_STATE: u32 = 2;
pub const DATA_CALL_DORMANCY: u32 = 4;
pub const LINK_CAPACITY: u32 = 8;
pub const PHYSICAL_CHANNEL_CONFIG: u32 = 16;
pub const REGISTRATION_FAILURE: u32 = 32;
pub const BARRING_INFO: u32 = 64;
pub const KNOWN_BITS: u32 = 127;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Input { pub filter: u32 }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Control { Signal, Cs, Ps, Screen }

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Step {
    pub id: u64,
    pub channel: u8,
    pub command: String,
    pub control: Control,
    pub enabled: bool,
    pub fallback_on_error: Option<String>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PolicyError { UnexpectedCompletion, IdExhausted }

#[derive(Default)]
pub struct Policy {
    pub desired: Option<Input>,
    applied: [Option<bool>; 4],
    pending: Vec<Step>,
    next_id: u64,
    pub failed: Vec<Step>,
}

impl Policy {
    pub fn unknown_bits(&self) -> u32 { self.desired.map_or(0, |input| input.filter & !KNOWN_BITS) }
    pub fn registration_failure_enabled(&self) -> bool {
        self.desired.is_some_and(|input| input.filter & REGISTRATION_FAILURE != 0)
    }
    pub fn applied(&self, control: Control) -> Option<bool> { self.applied[control as usize] }

    pub fn reconcile(&mut self, input: Input) -> Result<Vec<Step>, PolicyError> {
        self.desired = Some(input);
        if !self.pending.is_empty() { return Ok(Vec::new()); }
        let signal = input.filter & SIGNAL_STRENGTH != 0;
        let network = input.filter & FULL_NETWORK_STATE != 0;
        let reporting = signal || network;
        let controls = [
            (Control::Signal, signal, "ECSQ", 1),
            (Control::Cs, network, "EREG", 1),
            (Control::Ps, network, "EGREG", 1),
            (Control::Screen, reporting, "ESCREENSTATE", 3),
        ];
        let needed = controls.iter().filter(|(control, enabled, _, _)| self.applied[*control as usize] != Some(*enabled)).count();
        self.next_id.checked_add(needed as u64).ok_or(PolicyError::IdExhausted)?;
        for (control, enabled, name, channel) in controls {
            if self.applied[control as usize] == Some(enabled) { continue; }
            let registration = matches!(control, Control::Cs | Control::Ps);
            let mode = if registration { if enabled { 3 } else { 9 } } else { u8::from(enabled) };
            self.next_id += 1;
            self.pending.push(Step {
                id: self.next_id, channel, command: format!("AT+{name}={mode}"), control, enabled,
                fallback_on_error: (registration && !enabled).then(|| format!("AT+{name}=5")),
            });
        }
        Ok(self.pending.clone())
    }

    pub fn complete(&mut self, id: u64, success: bool) -> Result<Option<Step>, PolicyError> {
        if self.pending.first().map(|step| step.id) != Some(id) { return Err(PolicyError::UnexpectedCompletion); }
        if !success && self.pending[0].fallback_on_error.is_some() {
            let next_id = self.next_id.checked_add(1).ok_or(PolicyError::IdExhausted)?;
            self.next_id = next_id;
            let step = &mut self.pending[0];
            step.command = step.fallback_on_error.take().unwrap();
            step.id = next_id;
            return Ok(Some(step.clone()));
        }
        let step = self.pending.remove(0);
        if success { self.applied[step.control as usize] = Some(step.enabled); }
        else { self.failed.push(step); }
        Ok(None)
    }

    pub fn reconnect(&mut self) {
        self.applied = [None; 4];
        self.pending.clear();
    }
}
