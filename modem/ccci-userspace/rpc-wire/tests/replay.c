/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define __packed __attribute__((packed))
#define GFP_ATOMIC 0
#define RPC 0
#define CCCI_DEBUG_LOG(md, tag, ...) ((void)(md))
#define CCCI_ERROR_LOG(md, tag, ...) ((void)(md))

@CONSTANTS@
@HEADER@
@RPC_PACKET@
@RPC_BUFFER@

_Static_assert(sizeof(struct ccci_header) == 16, "CCCI header must be 16 bytes");
_Static_assert(sizeof(struct rpc_buffer) == 24, "RPC record prefix must be 24 bytes");
_Static_assert(offsetof(struct rpc_buffer, op_id) == 16, "RPC opcode offset");
_Static_assert(offsetof(struct rpc_buffer, para_num) == 20, "RPC count offset");
_Static_assert(offsetof(struct rpc_buffer, buffer) == 24, "RPC argument-area offset");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Replay requires a little-endian host");

struct port_t { int md_id; };
struct sk_buff { unsigned int len; unsigned char *data; size_t capacity; };
static unsigned int sent;

static void *kmalloc(size_t size, int flags)
{
    (void)flags;
    return calloc(1, size);
}

static void kfree(void *pointer) { free(pointer); }
static void ccci_free_skb(struct sk_buff *skb) { (void)skb; }

static void skb_put(struct sk_buff *skb, unsigned int bytes)
{
    if (bytes > skb->capacity - skb->len) abort();
    skb->len += bytes;
}

static void skb_trim(struct sk_buff *skb, unsigned int length)
{
    if (length > skb->len) abort();
    skb->len = length;
}

static void ccci_rpc_work_helper(struct port_t *port, struct rpc_pkt *packets,
                                struct rpc_buffer *buffer, unsigned int *temporary)
{
    (void)port;
    (void)packets;
    (void)buffer;
    (void)temporary;
}

static void write_word(uint32_t value)
{
    unsigned char bytes[4] = { value, value >> 8, value >> 16, value >> 24 };
    if (fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes)) abort();
}

static int port_send_skb_to_md(struct port_t *port, struct sk_buff *skb, int blocking)
{
    (void)port;
    (void)blocking;
    sent++;
    write_word(skb->len);
    if (fwrite(skb->data, 1, skb->len, stdout) != skb->len) abort();
    return 0;
}

@HANDLER@

int main(void)
{
    unsigned char length_bytes[4];
    while (fread(length_bytes, 1, sizeof(length_bytes), stdin) == sizeof(length_bytes)) {
        uint32_t length = (uint32_t)length_bytes[0] | ((uint32_t)length_bytes[1] << 8)
            | ((uint32_t)length_bytes[2] << 16) | ((uint32_t)length_bytes[3] << 24);
        if (length < sizeof(struct rpc_buffer) || length > RPC_MAX_BUF_SIZE + 4) abort();
        _Alignas(8) unsigned char data[RPC_MAX_BUF_SIZE + 32] = { 0 };
        if (fread(data, 1, length, stdin) != length) abort();
        struct port_t port = { .md_id = 0 };
        struct sk_buff skb = { .len = length, .data = data, .capacity = sizeof(data) };
        unsigned int before = sent;
        rpc_msg_handler(&port, &skb);
        if (sent == before) write_word(0);
    }
    return ferror(stdin) || fflush(stdout) ? 1 : 0;
}
