/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(MINDONE_AUDIO_RUST_ROUTES)
#include "AudioRustRoutes.h"
#endif

static const char *actions[8];
static unsigned int action_count;
static unsigned int checks;
static int recording;

static void check(int condition, const char *message)
{
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void record_action(const char *action)
{
    if (!recording)
        return;
    check(action_count < 8, "bounded mixer action count");
    actions[action_count++] = action;
}

PRODUCTION_FIXTURE

static void reset_actions(void)
{
    action_count = 0;
    recording = 1;
}

static void trace_state(const struct mindone_audio_device *device)
{
    for (unsigned int i = 0; i < OUT_DEVICE_COUNT; i++)
        printf("%d,", device->out_ep_users[i]);
    for (unsigned int i = 0; i < action_count; i++)
        printf("%s,", actions[i]);
    for (unsigned int i = 0; i < 5; i++)
        printf("%s,", device->route->applied[i].value);
    putchar('\n');
}

static void ordering(void)
{
    struct audio_route route;
    struct mindone_audio_device device;
    const enum output_device pair[] = {OUT_DEVICE_SPEAKER, OUT_DEVICE_EARPIECE};
    for (unsigned int i = 0; i < 2; i++) {
        recording = 0;
        init(&device, &route);
        endpoint_acquire(&device, pair[i]);
        endpoint_acquire(&device, pair[1 - i]);
        route.require_receiver = true;
        reset_actions();
        apply_output_route(&device, pair[i], OUT_DEVICE_HEADPHONE);
        check(action_count == 5, "shared release and acquire action count");
        const char *expected[] = {output_path_off_name(pair[i]), output_path_name(pair[1 - i]),
                                  "update", "headphone", "update"};
        for (unsigned int j = 0; j < 5; j++)
            check(strcmp(actions[j], expected[j]) == 0, "release restore update precedes acquire update");
        expect(&device);
    }
    recording = 0;
}

static uint32_t random_step(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

static void replay(int trace)
{
    struct audio_route route;
    struct mindone_audio_device device;
    enum output_device owners[64] = {0};
    uint32_t seed = 0x98761234;
    init(&device, &route);
    for (unsigned int step = 0; step < 10000; step++) {
        unsigned int owner = (random_step(&seed) >> 16) % (step < 5000 ? 3 : 64);
        enum output_device next = (enum output_device)(random_step(&seed) % OUT_DEVICE_COUNT);
        enum output_device old = owners[owner];
        int receiver_others = 0;
        for (unsigned int i = 0; i < 64; i++)
            if (i != owner && (owners[i] == OUT_DEVICE_SPEAKER || owners[i] == OUT_DEVICE_EARPIECE))
                receiver_others++;
        route.require_receiver = receiver_others > 0;
        reset_actions();
        if (old == OUT_DEVICE_NONE && step % 2)
            endpoint_acquire(&device, next);
        else if (next == OUT_DEVICE_NONE && step % 2)
            teardown_output_route(&device, old);
        else
            apply_output_route(&device, old, next);
        owners[owner] = next;
        int expected[OUT_DEVICE_COUNT] = {0};
        for (unsigned int i = 0; i < 64; i++)
            if (owners[i] != OUT_DEVICE_NONE)
                expected[owners[i]]++;
        check(memcmp(device.out_ep_users, expected, sizeof(expected)) == 0,
              "ownership equals the active logical owners");
        expect(&device);
        if (trace)
            trace_state(&device);
    }
    route.require_receiver = false;
    for (unsigned int i = 0; i < 64; i++) {
        reset_actions();
        teardown_output_route(&device, owners[i]);
        owners[i] = OUT_DEVICE_NONE;
        expect(&device);
        if (trace)
            trace_state(&device);
    }
    for (unsigned int i = 0; i < OUT_DEVICE_COUNT; i++)
        check(device.out_ep_users[i] == 0, "final owner powers down");
    recording = 0;
}

#if defined(MINDONE_AUDIO_RUST_ROUTES)
static void ffi_errors(void)
{
    struct mindone_audio_route_state state = {{0}};
    struct mindone_audio_route_plan output;
    struct mindone_audio_route_plan sentinel;
    memset(&sentinel, 0x5a, sizeof(sentinel));
    output = sentinel;
    check(mindone_audio_route_plan(NULL, 0, 1, &output) == -1, "null input rejected");
    check(mindone_audio_route_plan(&state, 0, 1, NULL) == -1, "null output rejected");
    check(mindone_audio_route_plan(&state, UINT32_MAX, 1, &output) == -2, "invalid old endpoint rejected");
    check(mindone_audio_route_plan(&state, 0, 5, &output) == -2, "invalid new endpoint rejected");
    check(mindone_audio_route_plan(&state, 1, 2, &output) == -4, "unowned reroute rejected");
    state.users[0] = 1;
    check(mindone_audio_route_plan(&state, 0, 1, &output) == -3, "NONE owner rejected");
    state.users[0] = 0;
    state.users[4] = -1;
    check(mindone_audio_route_plan(&state, 0, 1, &output) == -3, "negative count rejected");
    state.users[4] = 0;
    state.users[1] = 1;
    state.users[2] = INT32_MAX;
    check(mindone_audio_route_plan(&state, 1, 2, &output) == -5, "overflow reroute rejected");
    check(memcmp(&output, &sentinel, sizeof(output)) == 0, "failed FFI preserves output bytes");
    _Alignas(4) unsigned char storage[sizeof(output) + 4];
    memset(storage, 0x3c, sizeof(storage));
    check(mindone_audio_route_plan((const struct mindone_audio_route_state *)(storage + 1),
                                  0, 1, &output) == -1, "unaligned input rejected");
    check(mindone_audio_route_plan(&state, 0, 1,
                                  (struct mindone_audio_route_plan *)(storage + 1)) == -1,
          "unaligned output rejected");
    for (unsigned int i = 0; i < sizeof(storage); i++)
        check(storage[i] == 0x3c, "unaligned output not written");
    state.users[2] = 0;
    state.users[1] = INT32_MAX - 1;
    check(mindone_audio_route_plan(&state, 0, 1, &output) == 0, "i32 final owner accepted");
    check(output.next.users[1] == INT32_MAX && output.action_count == 0, "i32 count without mixer action");
    for (unsigned int i = 0; i < 5; i++)
        check(output.actions[i].kind == 0 && output.actions[i].endpoint == 0, "unused actions zeroed");
    union {
        struct mindone_audio_route_state state;
        struct mindone_audio_route_plan plan;
    } overlap = {.state = {{0}}};
    check(mindone_audio_route_plan(&overlap.state, 0, 1, &overlap.plan) == 0,
          "input snapshot permits aligned alias");
    check(overlap.plan.next.users[1] == 1 && overlap.plan.action_count == 2, "aliased output valid");
}

static void adapter_errors(void)
{
    struct audio_route route;
    struct mindone_audio_device device;
    init(&device, &route);
    reset_actions();
    check(check_endpoint_transition(&device, OUT_DEVICE_SPEAKER, OUT_DEVICE_HEADPHONE) == -EINVAL,
          "preflight rejects unowned transition");
    endpoint_acquire(&device, (enum output_device)-1);
    endpoint_release(&device, (enum output_device)OUT_DEVICE_COUNT);
    apply_output_route(&device, OUT_DEVICE_SPEAKER, OUT_DEVICE_HEADPHONE);
    int empty[OUT_DEVICE_COUNT] = {0};
    check(action_count == 0 && memcmp(device.out_ep_users, empty, sizeof(empty)) == 0,
          "invalid adapter input has no side effects");
    recording = 0;
    endpoint_acquire(&device, OUT_DEVICE_SPEAKER);
    endpoint_acquire(&device, OUT_DEVICE_HEADPHONE);
    device.out_ep_users[OUT_DEVICE_HEADPHONE] = INT32_MAX;
    struct mindone_audio_device before = device;
    struct audio_route route_before = route;
    reset_actions();
    apply_output_route(&device, OUT_DEVICE_SPEAKER, OUT_DEVICE_HEADPHONE);
    check(action_count == 0 && memcmp(device.out_ep_users, before.out_ep_users,
                                     sizeof(before.out_ep_users)) == 0 &&
          memcmp(&route, &route_before, sizeof(route)) == 0,
          "overflow preserves owners and mixer");
    device.out_ep_users[OUT_DEVICE_HEADPHONE] = -1;
    before = device;
    apply_output_route(&device, OUT_DEVICE_SPEAKER, OUT_DEVICE_EARPIECE);
    check(action_count == 0 && memcmp(device.out_ep_users, before.out_ep_users,
                                     sizeof(before.out_ep_users)) == 0,
          "invalid snapshot rejects complete transition");
    recording = 0;
}
#endif

int main(int argc, char **argv)
{
    int trace = argc == 2 && strcmp(argv[1], "trace") == 0;
    baseline_cases();
    ordering();
    replay(trace);
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    if (!trace) {
        ffi_errors();
        adapter_errors();
    }
#endif
    if (!trace)
        printf("%u checks: 10000 exact ownership replays and shared mixer ordering PASS\n", checks);
    return 0;
}
