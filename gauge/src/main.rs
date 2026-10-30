// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#![forbid(unsafe_code)]

#[cfg(any(
    target_os = "android",
    all(target_os = "linux", feature = "raw-netlink")
))]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    mindone_gauge::daemon::run()
}

#[cfg(not(any(
    target_os = "android",
    all(target_os = "linux", feature = "raw-netlink")
)))]
fn main() {
    eprintln!("mindone_gauged requires Android or Linux with raw-netlink");
    std::process::exit(1);
}
