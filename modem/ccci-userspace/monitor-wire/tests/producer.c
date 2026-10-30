/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
#define __packed __attribute__((packed))
#define CCCI_ERR_INVALID_PARAM 1
#define CCCI_ERR_ASSERT_ERR 2
#define unlikely(value) (value)
#define CCCI_ERROR_LOG(md, tag, ...) ((void)(md))

@CONSTANTS@
@HEADER@
@MESSAGES@

_Static_assert(sizeof(struct ccci_header) == 16, "CCCI monitor record must be 16 bytes");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Producer proof requires a little-endian host");

struct sk_buff { unsigned int len; unsigned char data[16]; };
struct ccci_fsm_monitor { int md_id; int rx_skb_list; int rx_wq; };
struct ccci_fsm_ctl { struct ccci_fsm_monitor monitor_ctl; };
static struct sk_buff packet;
static struct ccci_fsm_ctl controller;

static struct ccci_fsm_ctl *fsm_get_entity_by_md_id(int id)
{
    if (id != 0) abort();
    return &controller;
}

static int in_interrupt(void) { return 0; }

static struct sk_buff *ccci_alloc_skb(size_t length, int from_pool, int blocking)
{
    (void)from_pool;
    (void)blocking;
    if (length != sizeof(packet.data)) abort();
    packet.len = 0;
    memset(packet.data, 0xa5, sizeof(packet.data));
    return &packet;
}

static void *skb_put(struct sk_buff *skb, size_t length)
{
    if (length > sizeof(skb->data) - skb->len) abort();
    void *result = skb->data + skb->len;
    skb->len += length;
    return result;
}

static void ccci_skb_enqueue(int *queue, struct sk_buff *skb)
{
    (void)queue;
    if (skb->len != sizeof(skb->data)) abort();
    if (fwrite(skb->data, 1, skb->len, stdout) != skb->len) abort();
}

static void wake_up_all(int *queue) { (void)queue; }

@PRODUCER@

static uint32_t read_word(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int main(void)
{
    unsigned char bytes[8];
    while (fread(bytes, 1, sizeof(bytes), stdin) == sizeof(bytes)) {
        if (fsm_monitor_send_message(0, (enum CCCI_MD_MSG)read_word(bytes), read_word(bytes + 4))) abort();
    }
    return ferror(stdin) || fflush(stdout) ? 1 : 0;
}
