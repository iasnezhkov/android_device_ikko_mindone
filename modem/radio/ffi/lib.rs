/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![deny(unsafe_code)]
#![crate_name = "mindone_radio_ffi"]
#![crate_type = "staticlib"]

mod arena;
mod convert;
#[allow(unsafe_code)]
mod exports;
pub mod wire;
