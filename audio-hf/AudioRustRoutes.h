/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MINDONE_AUDIO_RUST_ROUTES_H
#define MINDONE_AUDIO_RUST_ROUTES_H

#include <stddef.h>
#include <stdint.h>

struct mindone_audio_route_state {
    int32_t users[5];
};

struct mindone_audio_route_action {
    uint32_t kind;
    uint32_t endpoint;
};

struct mindone_audio_route_plan {
    struct mindone_audio_route_state next;
    uint32_t action_count;
    struct mindone_audio_route_action actions[5];
};

_Static_assert(sizeof(struct mindone_audio_route_state) == 20, "route state size");
_Static_assert(_Alignof(struct mindone_audio_route_state) == 4, "route state alignment");
_Static_assert(sizeof(struct mindone_audio_route_action) == 8, "route action size");
_Static_assert(_Alignof(struct mindone_audio_route_action) == 4, "route action alignment");
_Static_assert(offsetof(struct mindone_audio_route_action, kind) == 0, "route action kind offset");
_Static_assert(offsetof(struct mindone_audio_route_action, endpoint) == 4, "route endpoint offset");
_Static_assert(sizeof(struct mindone_audio_route_plan) == 64, "route plan size");
_Static_assert(_Alignof(struct mindone_audio_route_plan) == 4, "route plan alignment");
_Static_assert(offsetof(struct mindone_audio_route_plan, next) == 0, "route next offset");
_Static_assert(offsetof(struct mindone_audio_route_plan, action_count) == 20, "route count offset");
_Static_assert(offsetof(struct mindone_audio_route_plan, actions) == 24, "route actions offset");

int32_t mindone_audio_route_plan(const struct mindone_audio_route_state *current,
                               uint32_t old_device, uint32_t new_device,
                               struct mindone_audio_route_plan *output);

#endif
