/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <limits.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

@DEFINES@

static int escalation_logs;

static void ignored_log(const char *format, ...)
{
    if (strstr(format, "recreating mux session"))
        escalation_logs++;
}

#define LOGE(...) ignored_log(__VA_ARGS__)
#define LOGW(...) ignored_log(__VA_ARGS__)
#define LOGI(...) ignored_log(__VA_ARGS__)
#define LOGD(...) ignored_log(__VA_ARGS__)

@CHANNEL@
@PHASE@

static struct chan ctl;
static struct chan chans[MAX_CHANS];
static struct chan *by_dlci[MAX_DLCI];
static int nchans = 2;
static enum mux_state state;
static int unresolved;
static bool setup_done;
static bool ril_started;
static int64_t setup_deadline;
static int64_t recovery_escalation_after;
static int64_t clock_ms;
static bool pf_echo;
static int frame_size = 512;
static uint8_t crc_table[256];
static uint8_t txq[TXQ_CAP];
static size_t txq_len;
static bool tx_blocked;
static int serial_fd = -1;
static size_t rx_len;
static size_t chat_len;
static int term_signal;
static int rejected_endpoint = 255;
static int restart_count;
static int endpoint_opens;
static int endpoint_closes;

static void print_hex(const uint8_t *bytes, size_t length)
{
    for (size_t index = 0; index < length; ++index)
        printf("%02x", bytes[index]);
    putchar('\n');
}

@ENCODER@

static bool tx_frame(uint8_t channel, uint8_t control, const uint8_t *payload, size_t length)
{
    bool accepted = source_tx_frame(channel, control, payload, length);

    if (!accepted)
        exit(3);
    printf("TX ");
    print_hex(txq, txq_len);
    txq_len = 0;
    return true;
}

static int64_t now_ms(void)
{
    return clock_ms;
}

static void prop_set(const char *key, const char *value)
{
    if (!strcmp(key, "vendor.ril.mtk.restart")) {
        assert(!strcmp(value, "1"));
        restart_count++;
    }
    if (!strcmp(key, "vendor.lackof.atci.channel")) {
        if (strcmp(value, "1"))
            exit(3);
        puts("ATCI");
    }
}

static void drop_root(void)
{
    int refused = 0;

    for (int index = 0; index < nchans; ++index)
        refused += chans[index].refused;
    printf("READY %d\n", refused);
}

static int chan_open_pty(struct chan *channel)
{
    endpoint_opens++;
    printf("OPEN %u\n", channel->dlci);
    if (channel->dlci == rejected_endpoint)
        return -1;
    channel->fd = 100 + channel->dlci;
    channel->pending_len = 0;
    channel->peer_fc = false;
    channel->local_fc = false;
    return 0;
}

static void chan_close_pty(struct chan *channel)
{
    endpoint_closes++;
    printf("CLOSE %u\n", channel->dlci);
    channel->fd = -1;
    channel->pending_len = 0;
    channel->peer_fc = false;
    channel->local_fc = false;
}

static void report_and_exit(const char *reason)
{
    fprintf(stderr, "Unexpected fatal C path: %s\n", reason);
    exit(3);
}

static int tx_flush(void)
{
    return 0;
}

static int tx_drain(int64_t deadline)
{
    (void)deadline;
    return 0;
}

static int serial_wait(int64_t deadline)
{
    (void)deadline;
    return 0;
}

static ssize_t serial_read(void)
{
    return 0;
}

static void control_command(const uint8_t *data, size_t length)
{
    printf("CONTROL ");
    print_hex(data, length);
}

static void deliver(struct chan *channel, const uint8_t *data, size_t length)
{
    if (channel->fd < 0) {
        printf("DROP %u %zu\n", channel->dlci, length);
        return;
    }
    printf("DELIVER %u ", channel->dlci);
    print_hex(data, length);
}

@FUNCTIONS@

static void read_exact(void *output, size_t length)
{
    if (fread(output, 1, length, stdin) != length)
        exit(2);
}

static uint64_t number(size_t width)
{
    uint8_t bytes[8] = { 0 };
    uint64_t value = 0;

    read_exact(bytes, width);
    for (size_t index = 0; index < width; ++index)
        value |= (uint64_t)bytes[index] << (index * 8);
    return value;
}

static void snapshot(void)
{
    printf("STATE %d %d %d %d %d %lld\n", state, unresolved, setup_done,
           ril_started, pf_echo, (long long)setup_deadline);
    for (int index = -1; index < nchans; ++index) {
        struct chan *channel = index < 0 ? &ctl : &chans[index];
        bool readable = state != ST_CLOSING && state != ST_PEER_CLOSING &&
            setup_done && channel->fd >= 0 && (channel->open || channel->refused) &&
            !channel->peer_fc;

        printf("CHANNEL %u %d %d %d %d %d %d %d %d %d\n", channel->dlci,
               channel->fd >= 0, channel->sabm_sent, channel->open, channel->disc_sent,
               channel->refused, channel->refused_logged, channel->peer_fc,
               channel->local_fc, readable);
    }
}

static void recreate_session(int expected_restarts)
{
    assert(state == ST_CLOSING);
    close_mux();
    close_all_ptys();
    close_serial();
    assert(!mux_open());
    on_ua(&ctl);
    on_ua(&chans[0]);
    assert(restart_count == expected_restarts - 1 && !setup_done);
    on_ua(&chans[1]);
    assert(restart_count == expected_restarts && setup_done);
    on_ua(&chans[0]);
    setup_complete();
    assert(restart_count == expected_restarts);
}

static void recovery_checks(void)
{
    struct chan *channel = &chans[0];
    int descriptor;
    int opens;
    int closes;
    int64_t next_escalation;

    assert(!mux_open());
    for (int i = -1; i < nchans; i++)
        on_ua(i < 0 ? &ctl : &chans[i]);
    setup_complete();
    assert(setup_done && !restart_count);
    descriptor = channel->fd;
    strcpy(channel->link, "stable-link");
    strcpy(channel->slave, "stable-slave");
    opens = endpoint_opens;
    closes = endpoint_closes;
    on_dm(channel);
    assert(!channel->open && channel->fd == descriptor);
    assert(recovery_timeout(-1) == REOPEN_RETRY_MS);
    assert(channel->reopen_attempts == 0);
    clock_ms += REOPEN_RETRY_MS;
    assert(recovery_timeout(-1) == REOPEN_RETRY_MS);
    assert(channel->sabm_sent && channel->reopen_attempts == 1);
    on_ua(channel);
    assert(channel->open && !channel->reopen_deadline && unresolved == 0);
    clock_ms += REOPEN_RETRY_MS * 10;
    assert(recovery_timeout(-1) == -1);
    on_disc(channel->dlci, channel);
    clock_ms += REOPEN_RETRY_MS;
    recovery_timeout(-1);
    assert(channel->reopen_attempts == 2);
    on_dm(channel);
    assert(!channel->refused && channel->reopen_deadline);
    clock_ms += REOPEN_RETRY_MS;
    recovery_timeout(-1);
    assert(channel->reopen_attempts == REOPEN_ATTEMPTS);
    clock_ms += REOPEN_RETRY_MS;
    assert(recovery_timeout(-1) == 0 && state == ST_CLOSING);
    assert(escalation_logs == 1 && !channel->sabm_sent);
    assert(channel->fd == descriptor && !strcmp(channel->link, "stable-link"));
    assert(!strcmp(channel->slave, "stable-slave"));
    assert(endpoint_opens == opens && endpoint_closes == closes);
    next_escalation = recovery_escalation_after;
    assert(next_escalation == clock_ms + RECOVERY_ESCALATION_MS);
    recreate_session(1);
    assert(!channel->reopen_attempts && !channel->reopen_deadline);
    assert(recovery_escalation_after == next_escalation);
    on_disc(channel->dlci, channel);
    clock_ms += REOPEN_RETRY_MS;
    recovery_timeout(-1);
    handle_frame(channel->dlci, CTRL_SABM | CTRL_PF, NULL, 0);
    assert(channel->open && !channel->reopen_deadline);
    on_dm(&chans[1]);
    txq_len = sizeof(txq);
    for (int i = 0; i < REOPEN_ATTEMPTS; i++) {
        clock_ms += REOPEN_RETRY_MS;
        assert(recovery_timeout(-1) == REOPEN_RETRY_MS);
        assert(chans[1].reopen_attempts == (unsigned)i + 1);
        assert(!chans[1].sabm_sent && txq_len == sizeof(txq));
    }
    clock_ms += REOPEN_RETRY_MS;
    assert(recovery_timeout(-1) == next_escalation - clock_ms);
    txq_len = 0;
    assert(chans[1].reopen_deadline == next_escalation && chans[1].fd >= 0);
    assert(state == ST_RUN && escalation_logs == 1 && restart_count == 1);
    for (int i = 0; i < 10; i++) {
        clock_ms += REOPEN_RETRY_MS;
        assert(recovery_timeout(10) == 10);
        assert(state == ST_RUN && escalation_logs == 1);
    }
    clock_ms = next_escalation - 1;
    assert(recovery_timeout(-1) == 1 && state == ST_RUN);
    clock_ms++;
    assert(recovery_timeout(-1) == 0 && state == ST_CLOSING);
    assert(escalation_logs == 2);
    assert(recovery_escalation_after == clock_ms + RECOVERY_ESCALATION_MS);
    recreate_session(2);
    assert(!channel->reopen_attempts && !channel->reopen_deadline);
    state = ST_CLOSING;
    channel->disc_sent = true;
    handle_frame(channel->dlci, CTRL_UA | CTRL_PF, NULL, 0);
    assert(!channel->open && !channel->disc_sent && state == ST_CLOSING);
    puts("RECOVERY_PASS dm disc retry-budget stable-pty peer-sabm escalation rate-limit session-restart-once terminal-close");
}

int main(int argc, char **argv)
{
    uint8_t payload[512];
    int operation;
    bool started = false;

    ctl.fd = -1;
    by_dlci[0] = &ctl;
    chans[0].dlci = 1;
    chans[1].dlci = 26;
    chans[1].atci = true;
    for (int index = 0; index < nchans; ++index) {
        chans[index].fd = -1;
        by_dlci[chans[index].dlci] = &chans[index];
    }
    crc_init();
    if (argc == 2 && !strcmp(argv[1], "--recovery")) {
        recovery_checks();
        return 0;
    }
    while ((operation = getchar()) != EOF) {
        switch (operation) {
        case 1:
            clock_ms = (int64_t)number(8);
            if (started) {
                close_all_ptys();
                close_serial();
            }
            if (mux_open())
                return 3;
            started = true;
            break;
        case 2: {
            uint8_t channel = (uint8_t)number(1);
            uint8_t control = (uint8_t)number(1);
            uint32_t length = (uint32_t)number(4);

            if (channel >= 64 || length > sizeof(payload))
                return 2;
            read_exact(payload, length);
            handle_frame(channel, control, payload, length);
            break;
        }
        case 3: {
            uint8_t channel = (uint8_t)number(1);
            uint8_t signals = (uint8_t)number(1);

            apply_msc(channel, signals);
            break;
        }
        case 4: {
            uint8_t channel = (uint8_t)number(1);
            bool paused = number(1) != 0;
            struct chan *value = channel < 64 ? by_dlci[channel] : NULL;

            if (value && value->fd >= 0 && value->local_fc != paused) {
                value->local_fc = paused;
                send_msc(value, paused);
            }
            break;
        }
        case 5: {
            uint8_t channel = (uint8_t)number(1);

            if (channel != 1 && channel != 26)
                return 2;
            chan_reopen(by_dlci[channel]);
            break;
        }
        case 6:
            rejected_endpoint = (int)number(1);
            break;
        case 7: {
            uint8_t control = (uint8_t)number(1);
            uint32_t length = (uint32_t)number(4);

            if (length > sizeof(payload))
                return 2;
            read_exact(payload, length);
            tx_frame(0, control, payload, length);
            break;
        }
        case 8: {
            uint8_t channel = (uint8_t)number(1);
            struct chan *value;

            if (channel != 1 && channel != 26)
                return 2;
            value = by_dlci[channel];
            printf("APP %d\n", value->refused ? (value->refused_logged ? 2 : 1) : 0);
            if (value->refused)
                value->refused_logged = true;
            break;
        }
        case 9:
            close_mux();
            break;
        case 10: {
            int64_t now = (int64_t)number(8);
            bool pending = number(1) != 0;
            int64_t timeout = pending ? 10 : -1;

            if (state == ST_CLOSING || state == ST_PEER_CLOSING) {
                timeout = -1;
            } else if (!setup_done) {
                int64_t remaining = setup_deadline - now;

                if (remaining <= 0)
                    return 3;
                if (timeout < 0 || remaining < timeout)
                    timeout = remaining;
            }
            printf("POLL %lld\n", (long long)timeout);
            break;
        }
        default:
            return 2;
        }
        snapshot();
    }
    return ferror(stdin) ? 2 : 0;
}
