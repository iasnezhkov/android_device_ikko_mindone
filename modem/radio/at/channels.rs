/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::engine::{Engine, Error};

pub struct Channels { engines: Vec<(u8, Engine)> }

impl Channels {
    pub fn new(now: u64, ids: &[u8], prefixes: &[&str]) -> Result<Self, Error> {
        if ids.is_empty() || ids.iter().enumerate().any(|(i, id)| ids[..i].contains(id)) {
            return Err(Error::Configuration);
        }
        let engines = ids.iter().map(|id| Engine::new(now, prefixes).map(|engine| (*id, engine)))
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self { engines })
    }

    pub fn get_mut(&mut self, channel: u8) -> Option<&mut Engine> {
        self.engines.iter_mut().find(|(id, _)| *id == channel).map(|(_, engine)| engine)
    }

    pub fn get(&self, channel: u8) -> Option<&Engine> {
        self.engines.iter().find(|(id, _)| *id == channel).map(|(_, engine)| engine)
    }

    pub fn ids(&self) -> impl Iterator<Item = u8> + '_ { self.engines.iter().map(|(id, _)| *id) }
}
