/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

@DEFINES@

_Static_assert(TXQ_CAP == 65536 && RX_CAP == 65536 && PENDING_CAP == 4096 &&
               PTY_CHUNK == 1024 && FRAME_OVERHEAD == 7 && MAX_FRAME == 32767,
               "Unexpected production bounds");

static void ignored(const char *value, ...) { (void)value; }
#define LOGE(...) ignored(__VA_ARGS__)
#define LOGW(...) ignored(__VA_ARGS__)
#define LOGD(...) ignored(__VA_ARGS__)
static uint8_t crc_table[256];
static uint8_t txq[TXQ_CAP];
static size_t txq_len;
static bool tx_blocked;
static bool pf_echo;
static int frame_size;
static int serial_fd = 3;
static const char *serial_path = "source-fixture";
static bool reset_seen;
static unsigned calls;
static uint8_t kinds[32];
static uint32_t lengths[32];
static unsigned steps;
static unsigned step;
static uint8_t accepted[TXQ_CAP];
static size_t accepted_len;
static uint8_t pending[PENDING_CAP + MAX_FRAME];
@CHANNEL@
static struct chan channel;
static bool buffering;

static void send_msc(struct chan *value, bool paused) { (void)value; (void)paused; }
static void chan_reopen(struct chan *value) { (void)value; exit(2); }

static void prop_set(const char *key, const char *value)
{
    if (strcmp(key, "vendor.ril.mux.ee.md1") || strcmp(value, "1")) exit(2);
    reset_seen = true;
}

static ssize_t fixture_write(int fd, const void *data, size_t length)
{
    if (buffering) {
        if (fd != 7 || !length) exit(2);
        errno = EAGAIN;
        return -1;
    }
    if (fd != serial_fd || !length || accepted_len + length > sizeof(accepted)) exit(2);
    ++calls;
    unsigned current = step++;
    uint8_t kind = current < steps ? kinds[current] : 2;
    if (!kind) {
        size_t count = lengths[current] < length ? lengths[current] : length;
        memcpy(accepted + accepted_len, data, count);
        accepted_len += count;
        return (ssize_t)count;
    }
    if (kind == 5) return 0;
    errno = kind == 1 ? EINTR : kind == 2 ? EAGAIN : kind == 3 ? EBUSY : kind == 6 ? ENODEV : EIO;
    return -1;
}

@FUNCTIONS@

static unsigned byte(void)
{
    int value = getchar();
    if (value == EOF) exit(2);
    return (unsigned)value;
}

static uint32_t number(void)
{
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) result |= byte() << shift;
    return result;
}

static void read_bytes(uint8_t *output, size_t length)
{
    if (fread(output, 1, length, stdin) != length) exit(2);
}

static void dump(const uint8_t *data, size_t length)
{
    printf(" %zu:", length);
    for (size_t i = 0; i < length; ++i) printf("%02x", data[i]);
}

int main(void)
{
    crc_init();
    int operation;
    while ((operation = getchar()) != EOF) {
        if (operation == 1) {
            frame_size = (int)number();
            if (frame_size < 1 || frame_size > MAX_FRAME) return 2;
            txq_len = 0;
            memset(&channel, 0, sizeof(channel));
            channel.fd = 7;
            channel.pending = pending;
            pf_echo = false;
            puts("RESET");
        } else if (operation == 2) {
            uint8_t channel = (uint8_t)byte(), control = (uint8_t)byte();
            size_t length = number();
            uint8_t data[MAX_FRAME + 1];
            if (length > sizeof(data)) return 2;
            read_bytes(data, length);
            printf("FRAME %d", tx_frame(channel, control, data, length));
            dump(txq, txq_len);
            putchar('\n');
        } else if (operation == 3) {
            steps = byte();
            if (steps > sizeof(kinds)) return 2;
            for (unsigned i = 0; i < steps; ++i) {
                kinds[i] = (uint8_t)byte();
                lengths[i] = number();
            }
            step = calls = 0;
            accepted_len = 0;
            reset_seen = false;
            int result = tx_flush();
            unsigned stop = reset_seen ? 4 : result ? 3 : txq_len ? 1 : 0;
            printf("DRAIN %u %u", stop, calls);
            dump(accepted, accepted_len);
            dump(txq, txq_len);
            putchar('\n');
        } else if (operation == 4) {
            size_t length = number();
            uint8_t data[MAX_FRAME];
            if (length > (size_t)frame_size) return 2;
            read_bytes(data, length);
            buffering = true;
            deliver(&channel, data, length);
            buffering = false;
            printf("PENDING");
            dump(pending, channel.pending_len);
            putchar('\n');
        } else if (operation == 5) {
            printf("ROOM %d\n", can_take_pty_data());
        } else return 2;
    }
    return ferror(stdin) ? 2 : 0;
}
