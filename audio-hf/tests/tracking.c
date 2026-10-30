/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
PRODUCTION_ENVIRONMENT

static struct mindone_audio_device *current_device;
static unsigned int live_allocations, allocation_calls, stream_initializations;
static int allocation_failure, mutex_failure, patch_on_free, trace_enabled;
static struct mindone_stream_out *allocated[320];

static int patch(struct mindone_audio_device *, unsigned int, audio_devices_t);
static void activate(struct mindone_stream_out *, struct pcm *);

static size_t registered(const struct mindone_audio_device *device)
{
    size_t count = 0;
    for (const struct mindone_stream_out *out = device->outs; out; out = out->next) {
        check(++count <= 320, "registry is finite and acyclic");
        check(out->dev == device, "registered stream belongs to device");
    }
    return count;
}

static void *stream_calloc(size_t count, size_t size)
{
    check(count == 1 && size == sizeof(struct mindone_stream_out), "actual output allocation layout");
    allocation_calls++;
    if (allocation_failure)
        return NULL;
    void *pointer = calloc(count, size);
    check(pointer != NULL, "host fixture allocation");
    for (unsigned int i = 0; i < 320; i++) {
        if (!allocated[i]) {
            allocated[i] = pointer;
            live_allocations++;
            return pointer;
        }
    }
    check(0, "fixture allocation slots available");
    return NULL;
}

static int stream_mutex_init(pthread_mutex_t *lock, const pthread_mutexattr_t *attributes)
{
    stream_initializations++;
    return mutex_failure ? mutex_failure : pthread_mutex_init(lock, attributes);
}

static void stream_free(void *pointer)
{
    if (!pointer)
        return;
    for (unsigned int i = 0; i < 320; i++) {
        if (allocated[i] != pointer)
            continue;
        struct mindone_stream_out *out = pointer;
        check(out->standby && !out->pcm && out->routed_device == OUT_DEVICE_NONE && !out->crossbar_on,
              "freed output has terminal owned resource state");
        pthread_mutex_lock(&current_device->lock);
        for (struct mindone_stream_out *other = current_device->outs; other; other = other->next)
            check(other != out, "freed output is detached from registry");
        pthread_mutex_unlock(&current_device->lock);
        if (patch_on_free)
            check(patch(current_device, (unsigned int)out->io_handle, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) == -EINVAL,
                  "patch during close rejects the unregistered output");
        allocated[i] = NULL;
        live_allocations--;
        break;
    }
    free(pointer);
}

PRODUCTION_CALLBACKS
PRODUCTION_FUNCTIONS

static void setup(struct mindone_audio_device *device, struct audio_route *route)
{
    check(!live_allocations, "prior fixture leaves no output allocations");
    *route = (struct audio_route){0};
    *device = (struct mindone_audio_device){.route = route};
    check(pthread_mutex_init(&device->lock, NULL) == 0, "device mutex initialized");
    current_device = device;
    allocation_failure = mutex_failure = patch_on_free = 0;
    allocation_calls = stream_initializations = 0;
    pcm_opens = pcm_closes = 0;
    change_on_open = NULL;
}

static void finish_device(struct mindone_audio_device *device)
{
    check(!device->outs && !live_allocations, "all outputs unregistered and freed");
    for (unsigned int i = 0; i < OUT_DEVICE_COUNT; i++)
        check(device->out_ep_users[i] == 0, "all endpoint owners released");
    pthread_mutex_destroy(&device->lock);
}

static struct mindone_stream_out *open_output(struct mindone_audio_device *device, int handle)
{
    struct audio_config config = {.sample_rate = 1234, .channel_mask = 777, .format = 888};
    struct mindone_stream_out *stream = (void *)(uintptr_t)1;
    int rc = mindone_audio_output_open(device, handle, AUDIO_DEVICE_OUT_SPEAKER,
                                    handle % 3 == 0 ? AUDIO_OUTPUT_FLAG_FAST :
                                    handle % 3 == 1 ? AUDIO_OUTPUT_FLAG_DEEP_BUFFER : 0,
                                    &config, &stream);
    check(rc == 0 && stream && stream != (void *)(uintptr_t)1, "open publishes a valid output");
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    check(config.sample_rate == OUT_SAMPLING_RATE && config.channel_mask == AUDIO_CHANNEL_OUT_STEREO &&
          config.format == AUDIO_FORMAT_PCM_16_BIT && out->io_handle == handle && out->dev == device,
          "successful open preserves normalized format and handle");
    check(out->standby && !out->pcm && out->routed_device == OUT_DEVICE_NONE && !out->next &&
          out->volume_left == 1.0f && out->volume_right == 1.0f && out->lim_gain == 1.0f &&
          out->frame_size == 2 * sizeof(int16_t),
          "successful open initializes complete standby stream");
    return out;
}

static int patch(struct mindone_audio_device *device, unsigned int handle, audio_devices_t endpoint)
{
    struct audio_port_config source = {.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = (int)handle};
    struct audio_port_config sink = {.type = AUDIO_PORT_TYPE_DEVICE, .ext.device.type = endpoint};
    audio_patch_handle_t output = 77;
    return mindone_audio_create_patch(device, 1, &source, 1, &sink, &output);
}

static int patch_live_outputs(struct mindone_audio_device *device, audio_devices_t endpoint)
{
    struct audio_port_config sources[320];
    unsigned int count = 0;
    for (struct mindone_stream_out *out = device->outs; out; out = out->next) {
        check(count < 320, "fixture patch source capacity");
        sources[count++] = (struct audio_port_config){.type = AUDIO_PORT_TYPE_MIX,
                                                     .ext.mix.handle = out->io_handle};
    }
    struct audio_port_config sink = {.type = AUDIO_PORT_TYPE_DEVICE, .ext.device.type = endpoint};
    audio_patch_handle_t handle = 77;
    return mindone_audio_create_patch(device, count, sources, 1, &sink, &handle);
}

static void invalid_patch_handles(void)
{
    for (unsigned int active = 0; active < 2; active++) {
        struct mindone_audio_device device;
        struct audio_route route;
        struct pcm pcms[2];
        struct mindone_stream_out *outputs[2];
        setup(&device, &route);
        outputs[0] = open_output(&device, 10);
        outputs[1] = open_output(&device, 12);
        check(outputs[0]->pcm_device != outputs[1]->pcm_device, "regression uses different output PCMs");
        struct mindone_stream_out *closed = open_output(&device, 11);
        mindone_audio_output_close(closed);
        if (active)
            for (unsigned int i = 0; i < 2; i++)
                activate(outputs[i], &pcms[i]);
        const audio_devices_t endpoints[] = {AUDIO_DEVICE_OUT_WIRED_HEADPHONE, AUDIO_DEVICE_OUT_BLUETOOTH_SCO};
        for (unsigned int endpoint = 0; endpoint < 2; endpoint++) {
            for (unsigned int scenario = 0; scenario < 8; scenario++) {
                struct audio_port_config sources[2] = {
                    {.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 10},
                    {.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 11}};
                struct audio_port_config sinks[2] = {
                    {.type = AUDIO_PORT_TYPE_DEVICE, .ext.device.type = endpoints[endpoint]},
                    {.type = AUDIO_PORT_TYPE_DEVICE, .ext.device.type = AUDIO_DEVICE_OUT_SPEAKER}};
                unsigned int source_count = 1, sink_count = 1;
                device.mode = AUDIO_MODE_NORMAL;
                if (scenario < 2)
                    sources[0].ext.mix.handle = scenario == 0 ? 9999 : 11;
                else if (scenario == 2)
                    source_count = 2;
                else if (scenario == 3) {
                    sources[0].ext.mix.handle = 11;
                    sources[1].ext.mix.handle = 12;
                    source_count = 2;
                } else if (scenario == 4) {
                    sources[1].type = AUDIO_PORT_TYPE_DEVICE;
                    source_count = 2;
                } else if (scenario == 5) {
                    sources[0].type = AUDIO_PORT_TYPE_DEVICE;
                    sources[0].ext.device.type = AUDIO_DEVICE_IN_VOICE_CALL;
                    source_count = 2;
                    device.mode = AUDIO_MODE_IN_CALL;
                } else if (scenario == 6)
                    sink_count = 2;
                else
                    sources[0].type = AUDIO_PORT_TYPE_DEVICE;
                struct mindone_audio_device saved_device = device;
                struct mindone_stream_out saved[2] = {*outputs[0], *outputs[1]};
                struct audio_route saved_route = route;
                unsigned int opens = pcm_opens, closes = pcm_closes, modem = modem_calls;
                audio_patch_handle_t handle = 77;
                check(mindone_audio_create_patch(&device, source_count, sources,
                                             sink_count, sinks, &handle) == -EINVAL,
                      "unknown stale or malformed output patch rejected");
                check(handle == 77 && memcmp(&device, &saved_device, sizeof(device)) == 0 &&
                      memcmp(&route, &saved_route, sizeof(route)) == 0,
                      "invalid patch preserves device counters mixer and patch handle");
                for (unsigned int i = 0; i < 2; i++) {
                    check(memcmp(outputs[i], &saved[i], sizeof(saved[i])) == 0,
                          "invalid patch preserves every selected and unrelated stream");
                    if (active)
                        check(pcms[i].alive, "invalid patch preserves active PCM");
                }
                check(pcm_opens == opens && pcm_closes == closes && modem_calls == modem,
                      "invalid patch performs no PCM or modem operations");
            }
        }
        device.mode = AUDIO_MODE_NORMAL;
        for (unsigned int i = 0; i < 2; i++)
            mindone_audio_output_close(outputs[i]);
        check(patch(&device, 10, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) == -EINVAL,
              "empty registry rejects stale output patch");
        finish_device(&device);
    }
}

static void trace(const char *name, struct mindone_audio_device *device)
{
    if (!trace_enabled)
        return;
    printf("%s:", name);
    for (unsigned int i = 0; i < OUT_DEVICE_COUNT; i++)
        printf("%d,", device->out_ep_users[i]);
    for (struct mindone_stream_out *out = device->outs; out; out = out->next)
        printf("%d/%u/%d/%d/%d;", out->io_handle, out->device, out->routed_device,
               out->standby, out->reopen_pending);
    printf("effects=%u\n", device->route->effects);
}

static void activate(struct mindone_stream_out *out, struct pcm *pcm)
{
    pthread_mutex_lock(&out->lock);
    pthread_mutex_lock(&out->dev->lock);
    enum output_device next = output_device_from_mask(out->device);
    check(endpoint_acquire(out->dev, next) == 0, "fixture acquires actual endpoint owner");
    *pcm = (struct pcm){.alive = 1};
    out->pcm = pcm;
    out->standby = false;
    out->routed_device = next;
    pthread_mutex_unlock(&out->dev->lock);
    pthread_mutex_unlock(&out->lock);
}

static void arbitrary_stream_count(void)
{
    const unsigned int sizes[] = {1, 8, 9, 65, 257};
    for (unsigned int sample = 0; sample < sizeof(sizes) / sizeof(sizes[0]); sample++) {
        struct mindone_audio_device device;
        struct audio_route route;
        struct mindone_stream_out *outputs[257];
        unsigned int count = sizes[sample];
        setup(&device, &route);
        for (unsigned int i = 0; i < count; i++) {
            outputs[i] = open_output(&device, (int)i + 10);
            check(registered(&device) == i + 1, "every successful open is tracked beyond eight");
        }
        struct mindone_stream_out *cursor = device.outs;
        for (unsigned int i = 0; i < count; i++) {
            check(cursor == outputs[i], "registry preserves opening order");
            cursor = cursor->next;
        }
        check(patch(&device, count + 9, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) == 0,
              "last registered handle patch succeeds");
        for (unsigned int i = 0; i < count; i++)
            check(outputs[i]->device == (i + 1 == count ? AUDIO_DEVICE_OUT_WIRED_HEADPHONE :
                                         AUDIO_DEVICE_OUT_SPEAKER), "matched patch changes only selected output");
        trace("selected-tail", &device);
        check(patch(&device, 9999, AUDIO_DEVICE_OUT_EARPIECE) == -EINVAL, "unmatched patch rejected");
        for (unsigned int i = 0; i < count; i++)
            check(outputs[i]->device == (i + 1 == count ? AUDIO_DEVICE_OUT_WIRED_HEADPHONE :
                                         AUDIO_DEVICE_OUT_SPEAKER), "unmatched patch preserves every output");
        trace("unmatched-rejected", &device);
        for (unsigned int i = 0; i < count; i += 2) {
            mindone_audio_output_close(outputs[i]);
            outputs[i] = NULL;
        }
        trace("close-alternating", &device);
        for (unsigned int i = 1; i < count; i += 2)
            mindone_audio_output_close(outputs[i]);
        struct mindone_stream_out *reopened = open_output(&device, 10);
        check(registered(&device) == 1 && device.outs == reopened, "reopen after complete teardown is tracked");
        mindone_audio_output_close(reopened);
        finish_device(&device);
    }
}

static void open_failures(void)
{
    struct mindone_audio_device device;
    struct audio_route route;
    setup(&device, &route);
    struct mindone_stream_out *outputs[12];
    for (unsigned int i = 0; i < 12; i++)
        outputs[i] = open_output(&device, (int)i + 20);
    struct audio_config config = {.sample_rate = 1234, .channel_mask = 17, .format = 19};
    const struct audio_config original = config;
    for (unsigned int fault = 0; fault < 5; fault++) {
        struct mindone_stream_out *stream = (void *)(uintptr_t)1;
        allocation_failure = fault == 0;
        mutex_failure = fault == 1 ? EAGAIN : 0;
        unsigned int calls = allocation_calls;
        int rc = mindone_audio_output_open(fault == 2 ? NULL : &device, 44,
                                        AUDIO_DEVICE_OUT_SPEAKER, 0, fault == 3 ? NULL : &config,
                                        fault == 4 ? NULL : &stream);
        check(rc == (fault == 0 ? -ENOMEM : fault == 1 ? -EAGAIN : -EINVAL),
              "failed open reports allocation initialization or argument error");
        check(fault == 4 || stream == NULL, "failed open does not publish stream pointer");
        check(registered(&device) == 12 && live_allocations == 12 &&
              memcmp(&config, &original, sizeof(config)) == 0, "failed open preserves registry and config");
        if (fault >= 2)
            check(allocation_calls == calls, "invalid open performs no allocation");
    }
    allocation_failure = mutex_failure = 0;
    struct mindone_stream_out *retry = open_output(&device, 44);
    check(registered(&device) == 13, "open after allocation or initialization failure recovers");
    mindone_audio_output_close(retry);
    for (unsigned int i = 0; i < 12; i++)
        mindone_audio_output_close(outputs[i]);
    finish_device(&device);
}

static void unsupported_mmap(void)
{
    struct mindone_audio_device device;
    struct audio_route route;
    setup(&device, &route);
    struct mindone_stream_out *existing = open_output(&device, 50);
    const audio_output_flags_t requests[] = {
        AUDIO_OUTPUT_FLAG_MMAP_NOIRQ,
        AUDIO_OUTPUT_FLAG_MMAP_NOIRQ | AUDIO_OUTPUT_FLAG_DIRECT,
        AUDIO_OUTPUT_FLAG_MMAP_NOIRQ | AUDIO_OUTPUT_FLAG_FAST,
        AUDIO_OUTPUT_FLAG_MMAP_NOIRQ | AUDIO_OUTPUT_FLAG_DEEP_BUFFER};
    for (unsigned int i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        struct audio_config config = {.sample_rate = 48000, .channel_mask = 3, .format = 1};
        struct audio_config saved_config = config;
        struct mindone_audio_device saved_device = device;
        struct mindone_stream_out saved_stream = *existing;
        unsigned int allocations = allocation_calls, initializations = stream_initializations;
        struct mindone_stream_out *stream = (void *)(uintptr_t)1;
        check(mindone_audio_output_open(&device, 51, AUDIO_DEVICE_OUT_SPEAKER, requests[i],
                                     &config, &stream) == -ENOSYS && !stream,
              "unsupported MMAP output is rejected without publishing a stream");
        check(memcmp(&config, &saved_config, sizeof(config)) == 0 &&
              memcmp(&device, &saved_device, sizeof(device)) == 0 &&
              memcmp(existing, &saved_stream, sizeof(saved_stream)) == 0 &&
              allocation_calls == allocations && stream_initializations == initializations &&
              live_allocations == 1 && !pcm_opens && !pcm_closes && !route.effects,
              "unsupported MMAP output preserves config registry and resources");
    }
    struct mindone_stream_out *retry = open_output(&device, 51);
    mindone_audio_output_close(retry);
    mindone_audio_output_close(existing);
    finish_device(&device);
}

static void overlapping_routes(void)
{
    struct mindone_audio_device device;
    struct audio_route route;
    struct mindone_stream_out *outputs[12];
    struct pcm pcms[12];
    setup(&device, &route);
    for (unsigned int i = 0; i < 12; i++) {
        outputs[i] = open_output(&device, (int)i + 100);
        activate(outputs[i], &pcms[i]);
    }
    check(device.out_ep_users[OUT_DEVICE_SPEAKER] == 12, "twelve active outputs own endpoint");
    check(patch(&device, 111, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) == 0 &&
          outputs[11]->routed_device == OUT_DEVICE_HEADPHONE &&
          device.out_ep_users[OUT_DEVICE_SPEAKER] == 11 && device.out_ep_users[OUT_DEVICE_HEADPHONE] == 1,
          "ninth and later active outputs participate in ownership routing");
    trace("active-tail", &device);
    check(patch_live_outputs(&device, AUDIO_DEVICE_OUT_EARPIECE) == 0 &&
          device.out_ep_users[OUT_DEVICE_SPEAKER] == 0 && device.out_ep_users[OUT_DEVICE_HEADPHONE] == 0 &&
          device.out_ep_users[OUT_DEVICE_EARPIECE] == 12, "explicit multi-source patch reroutes requested owners");
    trace("active-multi-source", &device);
    mindone_audio_output_close(outputs[5]);
    check(device.out_ep_users[OUT_DEVICE_EARPIECE] == 11 && registered(&device) == 11,
          "middle close preserves other active owners");
    patch_on_free = 1;
    mindone_audio_output_close(outputs[0]);
    patch_on_free = 0;
    check(registered(&device) == 10 && device.out_ep_users[OUT_DEVICE_EARPIECE] == 10,
          "close detaches before nested patch observer");
    for (unsigned int i = 1; i < 12; i++)
        if (i != 5)
            mindone_audio_output_close(outputs[i]);
    check(pcm_closes == 12, "each active output closes its PCM once");
    finish_device(&device);
}

static void sco_reopen(void)
{
    struct mindone_audio_device device;
    struct audio_route route;
    struct mindone_stream_out *outputs[12];
    setup(&device, &route);
    for (unsigned int i = 0; i < 12; i++) {
        outputs[i] = open_output(&device, (int)i + 200);
        outputs[i]->routed_device = OUT_DEVICE_BT_SCO;
    }
    check(mindone_audio_set_sco(&device, (struct mindone_audio_sco){MINDONE_AUDIO_SCO_WB}) == 0 && device.bt_wb,
          "SCO band accepted");
    for (unsigned int i = 0; i < 12; i++) {
        check(outputs[i]->reopen_pending, "SCO band reaches every tracked output");
        outputs[i]->routed_device = OUT_DEVICE_NONE;
        outputs[i]->reopen_pending = false;
    }
    struct pcm previous;
    activate(outputs[11], &previous);
    check(patch(&device, 211, AUDIO_DEVICE_OUT_BLUETOOTH_SCO) == 0 && outputs[11]->reopen_pending,
          "tail SCO patch stages reopen");
    int16_t data[8] = {0};
    check(out_write(outputs[11], data, sizeof(data)) == sizeof(data) &&
          outputs[11]->routed_device == OUT_DEVICE_BT_SCO && !outputs[11]->reopen_pending &&
          outputs[11]->hw_config.rate == 16000 && !previous.alive,
          "actual write reopens patched tail on latest SCO band");
    trace("sco-reopened", &device);
    for (unsigned int i = 0; i < 12; i++)
        mindone_audio_output_close(outputs[i]);
    finish_device(&device);
}

#if defined(MINDONE_AUDIO_RUST_ROUTES)
static void failed_close_with_tail_sibling(void)
{
    struct mindone_audio_device device;
    struct audio_route route;
    struct mindone_stream_out *outputs[12];
    struct pcm first_pcm, last_pcm;
    setup(&device, &route);
    for (unsigned int i = 0; i < 12; i++)
        outputs[i] = open_output(&device, (int)i + 400);
    activate(outputs[0], &first_pcm);
    activate(outputs[11], &last_pcm);
    outputs[0]->crossbar_on = outputs[11]->crossbar_on = true;
    outputs[11]->pcm_device = outputs[0]->pcm_device;
    outputs[0]->reopen_pending = true;
    device.active_out = outputs[0];
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 0;
    unsigned int effects = route.effects;
    mindone_audio_output_close(outputs[0]);
    check(!first_pcm.alive && last_pcm.alive && !device.active_out && registered(&device) == 11,
          "rejected standby close removes only its owned stream");
    check(route.effects == effects && outputs[11]->crossbar_on &&
          device.out_ep_users[OUT_DEVICE_SPEAKER] == 0,
          "failed close preserves far shared crossbar and rejected ownership snapshot");
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 1;
    for (unsigned int i = 1; i < 12; i++)
        mindone_audio_output_close(outputs[i]);
    finish_device(&device);
}

static void cumulative_atomicity(void)
{
    struct mindone_audio_device device;
    struct audio_route route;
    struct mindone_stream_out *outputs[12];
    struct pcm pcms[12];
    setup(&device, &route);
    for (unsigned int i = 0; i < 12; i++) {
        outputs[i] = open_output(&device, (int)i + 300);
        activate(outputs[i], &pcms[i]);
    }
    struct audio_port_config sources[2] = {{.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 310},
                                          {.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 311}};
    struct audio_port_config sink = {.type = AUDIO_PORT_TYPE_DEVICE,
                                     .ext.device.type = AUDIO_DEVICE_OUT_WIRED_HEADPHONE};
    for (unsigned int matched = 0; matched < 2; matched++) {
        device.out_ep_users[OUT_DEVICE_HEADPHONE] = INT_MAX - 1;
        sources[0].ext.mix.handle = matched ? 310 : 311;
        sources[1].ext.mix.handle = matched ? 311 : 310;
        struct mindone_stream_out saved[12];
        for (unsigned int i = 0; i < 12; i++)
            saved[i] = *outputs[i];
        int counts[OUT_DEVICE_COUNT];
        memcpy(counts, device.out_ep_users, sizeof(counts));
        unsigned int effects = route.effects;
        audio_patch_handle_t handle = 99;
        check(mindone_audio_create_patch(&device, 2, sources, 1, &sink, &handle) == -EOVERFLOW,
              "tail cumulative overflow rejected");
        check(memcmp(counts, device.out_ep_users, sizeof(counts)) == 0 &&
              effects == route.effects && handle == 99, "cumulative preflight preserves counts mixer and handle");
        for (unsigned int i = 0; i < 12; i++)
            check(memcmp(&saved[i], outputs[i], sizeof(saved[i])) == 0,
                  "cumulative preflight preserves every stream before publishing");
    }
    device.out_ep_users[OUT_DEVICE_HEADPHONE] = 0;
    for (unsigned int i = 0; i < 12; i++)
        mindone_audio_output_close(outputs[i]);
    finish_device(&device);
}
#endif

int main(int argc, char **argv)
{
    (void)argv;
    trace_enabled = argc > 1;
    unsupported_mmap();
    invalid_patch_handles();
    arbitrary_stream_count();
    open_failures();
    overlapping_routes();
    sco_reopen();
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    if (!trace_enabled) {
        cumulative_atomicity();
        failed_close_with_tail_sibling();
    }
#endif
    if (!trace_enabled)
        printf("%u checks: exact output tracking lifecycle PASS\n", checks);
    return 0;
}
