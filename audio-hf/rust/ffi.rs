// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
use mindone_audio_routes::{Action, Endpoint, Error, Owners, ACTION_CAPACITY, ENDPOINT_COUNT};
use std::mem::{align_of, offset_of, size_of};

#[cfg(not(all(target_pointer_width = "64", target_endian = "little")))]
compile_error!("The audio routing FFI requires a 64-bit little-endian target");

#[derive(Clone, Copy)]
#[repr(C)]
pub struct RouteState {
    pub users: [i32; ENDPOINT_COUNT],
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct RouteAction {
    pub kind: u32,
    pub endpoint: u32,
}

#[repr(C)]
pub struct RoutePlan {
    pub next: RouteState,
    pub action_count: u32,
    pub actions: [RouteAction; ACTION_CAPACITY],
}

const _: () = {
    assert!(size_of::<RouteState>() == 20 && align_of::<RouteState>() == 4);
    assert!(size_of::<RouteAction>() == 8 && align_of::<RouteAction>() == 4);
    assert!(offset_of!(RouteAction, kind) == 0 && offset_of!(RouteAction, endpoint) == 4);
    assert!(size_of::<RoutePlan>() == 64 && align_of::<RoutePlan>() == 4);
    assert!(offset_of!(RoutePlan, next) == 0);
    assert!(offset_of!(RoutePlan, action_count) == 20);
    assert!(offset_of!(RoutePlan, actions) == 24);
};

fn status(error: Error) -> i32 {
    match error {
        Error::Endpoint => -2,
        Error::State => -3,
        Error::Underflow => -4,
        Error::Overflow => -5,
    }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_audio_route_plan(
    current: *const RouteState,
    old: u32,
    new: u32,
    output: *mut RoutePlan,
) -> i32 {
    if current.is_null() || output.is_null()
        || current as usize % align_of::<RouteState>() != 0
        || output as usize % align_of::<RoutePlan>() != 0
    {
        return -1;
    }
    let plan = (|| {
        let old = Endpoint::try_from(old)?;
        let new = Endpoint::try_from(new)?;
        let current = unsafe { current.read() };
        Owners::new(current.users)?.route(old, new)
    })();
    let plan = match plan {
        Ok(plan) => plan,
        Err(error) => return status(error),
    };
    let actions = plan.actions.map(|action| match action {
        Action::None => RouteAction { kind: 0, endpoint: 0 },
        Action::Enable(endpoint) => RouteAction { kind: 1, endpoint: endpoint as u32 },
        Action::Disable(endpoint) => RouteAction { kind: 2, endpoint: endpoint as u32 },
        Action::Update => RouteAction { kind: 3, endpoint: 0 },
    });
    unsafe {
        output.write(RoutePlan {
            next: RouteState { users: plan.next.users() },
            action_count: plan.action_count as u32,
            actions,
        });
    }
    0
}
