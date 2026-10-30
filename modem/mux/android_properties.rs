/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_service::{Properties, Property, Value};

extern "C" {
    fn mindone_mux_set_property(property: u32, value: u32) -> i32;
}

pub struct AndroidProperties;

impl Properties for AndroidProperties {
    fn set(&mut self, property: Property, value: Value) -> bool {
        unsafe { mindone_mux_set_property(property as u32, value as u32) == 0 }
    }
}
