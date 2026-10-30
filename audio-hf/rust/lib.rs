// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
#![no_std]

pub const ENDPOINT_COUNT: usize = 5;
pub const ACTION_CAPACITY: usize = 5;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Endpoint {
    None = 0,
    Speaker = 1,
    Headphone = 2,
    Earpiece = 3,
    Sco = 4,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Endpoint,
    State,
    Underflow,
    Overflow,
}

impl TryFrom<u32> for Endpoint {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self, Error> {
        match value {
            0 => Ok(Self::None),
            1 => Ok(Self::Speaker),
            2 => Ok(Self::Headphone),
            3 => Ok(Self::Earpiece),
            4 => Ok(Self::Sco),
            _ => Err(Error::Endpoint),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    None,
    Enable(Endpoint),
    Disable(Endpoint),
    Update,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Owners {
    users: [i32; ENDPOINT_COUNT],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Transition {
    pub next: Owners,
    pub actions: [Action; ACTION_CAPACITY],
    pub action_count: usize,
}

impl Owners {
    pub fn new(users: [i32; ENDPOINT_COUNT]) -> Result<Self, Error> {
        if users[0] != 0 || users.iter().any(|&count| count < 0) {
            return Err(Error::State);
        }
        Ok(Self { users })
    }

    pub fn users(&self) -> [i32; ENDPOINT_COUNT] {
        self.users
    }

    pub fn route(self, old: Endpoint, new: Endpoint) -> Result<Transition, Error> {
        let mut transition = Transition {
            next: self,
            actions: [Action::None; ACTION_CAPACITY],
            action_count: 0,
        };
        if old == new {
            return Ok(transition);
        }
        if old != Endpoint::None {
            let users = &mut transition.next.users[old as usize];
            if *users == 0 {
                return Err(Error::Underflow);
            }
            *users -= 1;
            if *users == 0 && old != Endpoint::Sco {
                transition.push(Action::Disable(old));
                let shared = match old {
                    Endpoint::Speaker => Endpoint::Earpiece,
                    Endpoint::Earpiece => Endpoint::Speaker,
                    _ => Endpoint::None,
                };
                if transition.next.users[shared as usize] > 0 {
                    transition.push(Action::Enable(shared));
                }
                transition.push(Action::Update);
            }
        }
        if new != Endpoint::None {
            let users = &mut transition.next.users[new as usize];
            *users = users.checked_add(1).ok_or(Error::Overflow)?;
            if *users == 1 && new != Endpoint::Sco {
                transition.push(Action::Enable(new));
                transition.push(Action::Update);
            }
        }
        Ok(transition)
    }
}

impl Transition {
    fn push(&mut self, action: Action) {
        self.actions[self.action_count] = action;
        self.action_count += 1;
    }
}
