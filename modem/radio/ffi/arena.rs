/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use std::any::Any;

#[derive(Default)]
pub(crate) struct Arena {
    allocations: Vec<Box<dyn Any>>,
}

impl Arena {
    pub fn store<T: 'static>(&mut self, values: Vec<T>) -> (*const T, u32) {
        let len = u32::try_from(values.len()).expect("output length");
        if values.is_empty() {
            return (std::ptr::null(), 0);
        }
        let values = values.into_boxed_slice();
        let data = values.as_ptr();
        self.allocations.push(Box::new(values));
        (data, len)
    }
}
