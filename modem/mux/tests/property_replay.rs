/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_android_properties::AndroidProperties;
use mindone_mux_runtime::{Effects, Event};
use mindone_mux_service::{Properties, Property, Publication, Value};

fn main() {
    let mut publication = Publication::new(AndroidProperties);
    assert!(publication.initialize());
    assert!(publication.initialize());
    let ready = publication.apply(Event::Ready { refused_channels: 0 });
    assert_eq!(publication.apply(Event::Ready { refused_channels: 1 }), ready);
    assert_eq!(publication.apply(Event::AtciUnavailable), ready);
    assert_eq!(publication.apply(Event::ModemException), ready);
    assert_eq!(publication.rejected(), !ready);
    let mut backend = AndroidProperties;
    assert!(!backend.set(Property::RilReady, Value::Two));
    if ready {
        for (property, value) in [(Property::ReportCase, Value::One), (Property::Report, Value::One),
                                  (Property::ReportCase, Value::Two), (Property::Report, Value::One)] {
            assert!(backend.set(property, value));
        }
    }
    println!("PROPERTY_REPLAY_PASS ready={ready}");
}
