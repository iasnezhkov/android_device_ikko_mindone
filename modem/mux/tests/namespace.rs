/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_platform::PtyFactory;
use std::path::PathBuf;

fn main() {
    let root = PathBuf::from(std::env::var_os("MINDONE_NAMESPACE_TEST_DIRECTORY").unwrap());
    assert!(PtyFactory::new(&root, 1, false).is_err());
    assert!(PtyFactory::new(&root, 1, true).is_err());
    assert!(PtyFactory::radio_shared(0).is_err());
    assert!(PtyFactory::radio_shared(5).is_err());
    for count in 1..=4 {
        let factory = PtyFactory::radio_shared(count).unwrap();
        assert_eq!(factory.channels().len(), count * 11 + 2);
    }
    println!("RADIO_CONSTRUCTOR_PASS private_modes=2 invalid_bounds=2 valid_counts=4 board_path_verified=true");
}
