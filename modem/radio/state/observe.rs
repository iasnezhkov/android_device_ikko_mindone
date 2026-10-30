/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{model::{Registration, Signal, Update}, parse};

pub fn line(line: &str) -> Option<Update> {
    let line = line.trim();
    if line.starts_with("+ECSQ:") {
        return parse::ecsq(line).ok().map(|report| {
            let (rat, signal) = Signal::from_ecsq(&report);
            Update::Signal(rat, signal)
        });
    }
    let family = if line.starts_with("+EREG:") { Some(parse::Family::Ereg) }
        else if line.starts_with("+EGREG:") { Some(parse::Family::Egreg) } else { None };
    if let Some(family) = family {
        return parse::registration(line, family, parse::Context::Unsolicited).ok()
            .and_then(|report| Registration::from_report(&report))
            .map(|(domain, registration)| Update::Registration(domain, registration));
    }
    parse::sim(line).ok().map(Update::Sim)
}
