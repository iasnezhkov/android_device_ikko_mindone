/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

@DEFINES@

#define LOGE(...) ((void)0)
#define LOGD(...) ((void)0)

static int frame_size = 512;
static bool pf_echo;
static uint8_t crc_table[256];
static uint8_t txq[TXQ_CAP];
static size_t txq_len;
static uint8_t rxb[RX_CAP];
static size_t rx_len;

static void print_hex(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
        printf("%02x", data[i]);
    putchar('\n');
}

static void handle_frame(uint8_t channel, uint8_t control, const uint8_t *payload, size_t length)
{
    printf("FRAME %u %02x ", channel, control);
    print_hex(payload, length);
}

@FUNCTIONS@

static void read_exact(void *output, size_t length)
{
    if (fread(output, 1, length, stdin) != length)
        exit(2);
}

static uint32_t read_u32(void)
{
    uint8_t bytes[4];

    read_exact(bytes, sizeof(bytes));
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

int main(void)
{
    uint8_t payload[RX_CAP];
    int operation;

    crc_init();
    while ((operation = getchar()) != EOF) {
        switch (operation) {
        case 1: {
            uint8_t fields[2];
            uint32_t length;

            read_exact(fields, sizeof(fields));
            frame_size = (int)read_u32();
            length = read_u32();
            if (frame_size < 1 || frame_size > MAX_FRAME || length > sizeof(payload))
                return 2;
            read_exact(payload, length);
            txq_len = 0;
            printf("TX %d ", tx_frame(fields[0], fields[1], payload, length));
            print_hex(txq, txq_len);
            break;
        }
        case 2:
            frame_size = (int)read_u32();
            if (frame_size < 1 || frame_size > MAX_FRAME)
                return 2;
            rx_len = 0;
            printf("LIMIT %d\n", frame_size);
            break;
        case 3: {
            uint32_t length = read_u32();

            if (length > sizeof(rxb) - rx_len)
                return 2;
            read_exact(rxb + rx_len, length);
            rx_len += length;
            rx_parse();
            printf("PENDING ");
            print_hex(rxb, rx_len);
            break;
        }
        case 4:
            printf("RESET %zu\n", rx_len);
            rx_len = 0;
            break;
        default:
            return 2;
        }
    }
    return ferror(stdin) ? 2 : 0;
}
