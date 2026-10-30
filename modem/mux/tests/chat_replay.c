/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

@DEFINES@

_Static_assert(CHAT_CAP == 4096 && CHAT_TIMEOUT_MS == 5000 && TX_RETRY_MS == 10 &&
               EIND_WAIT_MS == 20000 && EIND_WAIT_USER_MS == 10000 && MAX_FRAME == 32767,
               "Unexpected production chat bounds");

static void ignored(const char *format, ...) { (void)format; }
#define LOGE(...) ignored(__VA_ARGS__)
#define LOGW(...) ignored(__VA_ARGS__)
#define LOGI(...) ignored(__VA_ARGS__)
static char chat_buf[CHAT_CAP];
static size_t chat_len;
static int64_t clock_ms;
static int term_signal;
static int serial_fd = 3;
static const char *serial_path = "host-fixture";
static int frame_size, baud_index, pin;
static bool user;
static unsigned fault_kind, write_chunk, mode;
static bool indication, cmux_ready, reject_first;
static uint8_t command[128];
static size_t command_len;
static unsigned commands, at_commands, write_calls;
static uint8_t packets[64][64];
static size_t lengths[64];
static int delays[64];
static unsigned queued, packet;

static int64_t now_ms(void) { return clock_ms; }
static bool user_build(void) { return user; }
static void modem_exception_exit(void) { exit(2); }
static void log_text(const char *dir, const char *value, size_t length) { (void)dir; (void)value; (void)length; }

static void queue(const uint8_t *data, size_t length, int delay)
{
    if (queued == 64 || length > sizeof(packets[0])) exit(2);
    memcpy(packets[queued], data, length);
    lengths[queued] = length;
    delays[queued++] = delay;
}

static void text(const char *value) { queue((const uint8_t *)value, strlen(value), 0); }

static ssize_t fixture_read(int fd, void *output, size_t capacity)
{
    if (fd != 3 || packet == queued || lengths[packet] > capacity) exit(2);
    size_t length = lengths[packet];
    memcpy(output, packets[packet++], length);
    return (ssize_t)length;
}

static ssize_t fixture_write(int fd, const void *input, size_t length)
{
    if (fd != 3 || !length) exit(2);
    if (++write_calls == 1 && fault_kind) {
        errno = fault_kind == 1 ? EINTR : EBUSY;
        return -1;
    }
    size_t accepted = length < write_chunk ? length : write_chunk;
    if (accepted > sizeof(command) - command_len) exit(2);
    memcpy(command + command_len, input, accepted);
    command_len += accepted;
    if (command[command_len - 1] == '\r') {
        printf("COMMAND ");
        for (size_t i = 0; i < command_len; ++i) printf("%02x", command[i]);
        putchar('\n');
        ++commands;
        queued = packet = 0;
        if (command_len == 3 && !memcmp(command, "AT\r", 3)) {
            ++at_commands;
            if (at_commands == 1 && mode == 1) text("\r\nNOK\r\n");
            else if (at_commands == 1 && mode == 2) text("OK");
            else if (at_commands == 1 && mode == 3) {
                for (unsigned i = 0; i < 60; ++i) queue((const uint8_t *)"+URC: 1\r\n", 9, 1000);
            } else text(at_commands == 1 && reject_first ? "\r\nERROR\r\n" : "\r\nOK\r\n");
        } else if (command_len >= 8 && !memcmp(command, "AT+CMUX=", 8)) {
            if (cmux_ready) text("\r\n+CMUX: READY\r\n");
            if (mode == 4) {
                static const uint8_t final[] = {'\r','\n','O','K','\r','\n',0xf9,0,3};
                queue(final, sizeof(final), 0);
            } else text("\r\nOK\r\n");
        } else text("\r\nOK\r\n");
        command_len = 0;
        write_calls = 0;
    }
    return (ssize_t)accepted;
}

static int fixture_poll(struct pollfd *descriptors, nfds_t count, int timeout)
{
    if (timeout < 0) exit(2);
    if (count == 1 && descriptors && descriptors[0].fd == 3 && packet < queued) {
        clock_ms += delays[packet];
        delays[packet] = 0;
        descriptors[0].revents = POLLIN;
        return 1;
    }
    if (count && (count != 1 || !descriptors || descriptors[0].fd != 3)) exit(2);
    clock_ms += timeout;
    return 0;
}

@FUNCTIONS@

static unsigned byte(void)
{
    int value = getchar();
    if (value == EOF) exit(2);
    return (unsigned)value;
}

static uint32_t number(unsigned first)
{
    uint32_t value = first;
    for (unsigned shift = 8; shift < 32; shift += 8) value |= byte() << shift;
    return value;
}

int main(void)
{
    unsigned index = 0;
    int first;
    while ((first = getchar()) != EOF) {
        frame_size = (int)number((unsigned)first);
        baud_index = (int)byte(); user = byte();
        pin = (int32_t)number(byte());
        fault_kind = byte(); write_chunk = byte();
        indication = byte(); cmux_ready = byte(); reject_first = byte(); mode = byte();
        if (!write_chunk || frame_size < 1 || frame_size > 32767 || mode > 4) return 2;
        memset(chat_buf, 0, sizeof(chat_buf));
        chat_len = command_len = commands = at_commands = write_calls = queued = packet = 0;
        clock_ms = 0;
        if (indication) text("\r\n+EIND: 128\r\n");
        printf("CASE %u\n", index++);
        int result = modem_handshake();
        printf("RESULT %d %" PRId64 " %u %zu\n", result, clock_ms, commands, chat_len);
    }
    return ferror(stdin) ? 2 : 0;
}
