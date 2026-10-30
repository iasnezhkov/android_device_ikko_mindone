/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

@DEFINES@
@CHANNEL@
@PHASE@

static void ignored(const char *format, ...) { (void)format; }
#define LOGE(...) ignored(__VA_ARGS__)
#define LOGW(...) ignored(__VA_ARGS__)
#define LOGI(...) ignored(__VA_ARGS__)
#define LOGD(...) ignored(__VA_ARGS__)
static struct chan channels[3];
static struct chan *by_dlci[MAX_DLCI];
static enum mux_state state;
static int frame_size;
static uint8_t txq[TXQ_CAP];
static size_t txq_len;
static uint8_t crc_table[256];
static bool pf_echo;

static void print_hex(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) printf("%02x", data[i]);
    putchar('\n');
}

@ENCODER@

static bool tx_frame(uint8_t channel, uint8_t control, const uint8_t *payload, size_t length)
{
    if (!source_tx_frame(channel, control, payload, length)) exit(2);
    printf("TX ");
    print_hex(txq, txq_len);
    txq_len = 0;
    return true;
}

@HANDLERS@

static unsigned byte(void)
{
    int value = getchar();
    if (value == EOF) exit(2);
    return (unsigned)value;
}

static uint32_t number(void)
{
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= byte() << shift;
    return value;
}

int main(void)
{
    int operation;
    crc_init();
    while ((operation = getchar()) != EOF) {
        if (operation == 1) {
            frame_size = (int)number();
            if (frame_size < 2 || frame_size > MAX_FRAME) return 2;
            pf_echo = false;
            txq_len = 0;
            memset(channels, 0, sizeof(channels));
            memset(by_dlci, 0, sizeof(by_dlci));
            const uint8_t ids[] = {0, 1, 26};
            for (unsigned i = 0; i < 3; ++i) {
                channels[i].dlci = ids[i];
                channels[i].open = true;
                by_dlci[ids[i]] = &channels[i];
            }
            puts("RESET");
        } else if (operation == 2) {
            uint8_t control = (uint8_t)byte();
            size_t length = number();
            uint8_t data[MAX_FRAME];
            if (length > (size_t)frame_size || fread(data, 1, length, stdin) != length) return 2;
            if (control & CTRL_PF) pf_echo = true;
            state = ST_RUN;
            control_command(data, length);
            printf("STATE %d %d %d %d %d\n", state == ST_PEER_CLOSING,
                   channels[0].peer_fc, channels[1].peer_fc, channels[2].peer_fc, pf_echo);
        } else return 2;
    }
    return ferror(stdin) ? 2 : 0;
}
