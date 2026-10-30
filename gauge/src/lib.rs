// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#![forbid(unsafe_code)]

pub mod proto;
pub mod sample;
pub mod shadow;
pub mod store;

#[cfg(any(
    target_os = "android",
    all(target_os = "linux", feature = "raw-netlink")
))]
pub mod daemon;
pub mod transport;

#[cfg(test)]
mod tests;
