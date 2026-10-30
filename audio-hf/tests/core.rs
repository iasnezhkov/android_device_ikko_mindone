// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
use mindone_audio_routes::{Action, Endpoint as E, Error, Owners};

fn state(users: [i32; 5]) -> Owners {
    Owners::new(users).unwrap()
}

#[test]
fn first_and_last_owner_control_power() {
    for endpoint in [E::Speaker, E::Headphone, E::Earpiece] {
        let first = state([0; 5]).route(E::None, endpoint).unwrap();
        assert_eq!(&first.actions[..first.action_count], &[Action::Enable(endpoint), Action::Update]);
        let second = first.next.route(E::None, endpoint).unwrap();
        assert_eq!(second.action_count, 0);
        let one = second.next.route(endpoint, E::None).unwrap();
        assert_eq!(one.action_count, 0);
        let last = one.next.route(endpoint, E::None).unwrap();
        assert_eq!(&last.actions[..last.action_count], &[Action::Disable(endpoint), Action::Update]);
        assert_eq!(last.next.users(), [0; 5]);
    }
}

#[test]
fn shared_receiver_is_restored_before_update() {
    for (moving, held) in [(E::Speaker, E::Earpiece), (E::Earpiece, E::Speaker)] {
        let plan = state([0, 1, 0, 1, 0]).route(moving, E::Headphone).unwrap();
        assert_eq!(plan.actions, [Action::Disable(moving), Action::Enable(held),
                                 Action::Update, Action::Enable(E::Headphone), Action::Update]);
        assert_eq!(plan.action_count, 5);
    }
}

#[test]
fn sco_and_noop_do_not_touch_the_mixer() {
    let first = state([0; 5]).route(E::None, E::Sco).unwrap();
    assert_eq!(first.action_count, 0);
    assert_eq!(first.next.users(), [0, 0, 0, 0, 1]);
    assert_eq!(first.next.route(E::Sco, E::None).unwrap().next.users(), [0; 5]);
    for endpoint in [E::None, E::Speaker, E::Headphone, E::Earpiece, E::Sco] {
        let plan = state([0; 5]).route(endpoint, endpoint).unwrap();
        assert_eq!(plan.action_count, 0);
        assert_eq!(plan.next.users(), [0; 5]);
    }
}

#[test]
fn rejected_plans_preserve_the_original() {
    let original = state([0, 1, i32::MAX, 0, 0]);
    assert_eq!(original.route(E::Speaker, E::Headphone), Err(Error::Overflow));
    assert_eq!(original.users(), [0, 1, i32::MAX, 0, 0]);
    assert_eq!(original.route(E::Earpiece, E::Speaker), Err(Error::Underflow));
    assert_eq!(Owners::new([1, 0, 0, 0, 0]), Err(Error::State));
    assert_eq!(Owners::new([0, -1, 0, 0, 0]), Err(Error::State));
    assert_eq!(E::try_from(5), Err(Error::Endpoint));
    assert_eq!(E::try_from(u32::MAX), Err(Error::Endpoint));
}

#[test]
fn count_bound_is_i32_not_stream_tracking_capacity() {
    let large = state([0, i32::MAX - 1, 0, 0, 0]).route(E::None, E::Speaker).unwrap();
    assert_eq!(large.next.users()[1], i32::MAX);
    assert_eq!(large.action_count, 0);
    assert_eq!(large.next.route(E::None, E::Speaker), Err(Error::Overflow));
    assert_eq!(large.next.route(E::Speaker, E::None).unwrap().next.users()[1], i32::MAX - 1);
    let mut owners = state([0; 5]);
    for _ in 0..100 {
        owners = owners.route(E::None, E::Speaker).unwrap().next;
    }
    assert_eq!(owners.users()[1], 100);
}

#[test]
fn action_bound_and_state_conservation_are_exhaustive() {
    for bits in 0..81 {
        let mut users = [0; 5];
        let mut rest = bits;
        for value in &mut users[1..] {
            *value = rest % 3;
            rest /= 3;
        }
        for old in 0..5 {
            for new in 0..5 {
                let result = state(users).route(E::try_from(old).unwrap(), E::try_from(new).unwrap());
                if old != new && old != 0 && users[old as usize] == 0 {
                    assert_eq!(result, Err(Error::Underflow));
                    continue;
                }
                let plan = result.unwrap();
                assert!(plan.action_count <= 5);
                assert!(plan.actions[plan.action_count..].iter().all(|&a| a == Action::None));
                let mut expected = users;
                if old != new {
                    if old != 0 { expected[old as usize] -= 1; }
                    if new != 0 { expected[new as usize] += 1; }
                }
                assert_eq!(plan.next.users(), expected);
            }
        }
    }
}
