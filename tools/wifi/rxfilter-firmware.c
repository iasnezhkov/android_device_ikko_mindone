#include "rxfilter-firmware.h"
#include "rxfilter-legacy.h"
#include "rxfilter-policy.h"
#include "rxfilter-query.h"

#include <stddef.h>

static uint32_t load32(const uint8_t* p) {
    uint32_t value;
    __builtin_memcpy(&value, __builtin_assume_aligned(p, 4), sizeof(value));
    return value;
}

static uint16_t load16(const uint8_t* p) {
    uint16_t value;
    __builtin_memcpy(&value, __builtin_assume_aligned(p, 2), sizeof(value));
    return value;
}

static uint8_t* address(uint32_t value) {
#ifdef RXFILTER_FIRMWARE_TEST
    return rxfilter_firmware_test_address(value);
#else
    return (uint8_t*)(uintptr_t)value;
#endif
}

static uint8_t* controls(void) {
    uint8_t* base = address(load32(address(0xf03adc2cu)));
    return base ? base + 0xf4 : NULL;
}

static void store32(uint8_t* p, uint32_t value) {
    __builtin_memcpy(__builtin_assume_aligned(p, 4), &value, sizeof(value));
}

#ifndef RXFILTER_FIRMWARE_TEST
extern uint32_t rxfilter_original_allocate(uint32_t, struct rxfilter_firmware_buffer*);
extern void rxfilter_original_reset(uint32_t);
extern uint32_t rxfilter_original_seek(uint32_t, const uint8_t*);
extern uint32_t rxfilter_original_reconfigure(void);
extern void rxfilter_original_assert_log(uint32_t, uint32_t);

static uint32_t gp(void) {
    uint32_t value;
    __asm__("mov55 %0, $gp" : "=r"(value));
    return value;
}
#endif

static __attribute__((noinline)) uint32_t reply(const uint8_t* header, uint8_t status,
    uint32_t nonce, _Bool extended) {
    uint32_t size = 32 + 8 * extended;
    struct rxfilter_firmware_buffer buffer;
#ifdef RXFILTER_FIRMWARE_TEST
    uint32_t result = rxfilter_firmware_test_allocate(size, &buffer);
#else
    uint32_t result = rxfilter_original_allocate(size, &buffer);
#endif
    if (result == 0xc0000001u)
        return 0x103;
    uint8_t* event = address(buffer.address) + 20;
    store32(event, 0xe0000000u | (size - 20));
    store32(event + 4, 0xfdu | (uint32_t)header[7] << 8);
    store32(event + 8, status | (uint32_t)header[4] << 8 |
        ((uint32_t)RXFILTER_QUERY_VERSION * extended << 16));
    if (extended) {
        store32(event + 12, nonce);
        uint32_t baseline = 0;
        if (status != RXFILTER_QUERY_STATUS_INVALID) {
            const uint8_t* state = controls() + header[36];
            baseline = state[0] | (uint32_t)state[4] << 8 | (uint32_t)state[8] << 16;
        }
        store32(event + 16, baseline);
    }
#ifdef RXFILTER_FIRMWARE_TEST
    rxfilter_firmware_test_prepare(buffer.address, size);
    uint32_t channel = rxfilter_firmware_test_channel();
    rxfilter_firmware_test_send(0, channel, 0, buffer.token);
#else
    ((void (*)(uint8_t*, uint32_t))(uintptr_t)load32(address(gp() - 0x21cf0u)))
        (address(buffer.address), size);
    void (*send)(uint32_t, uint32_t, uint32_t, uint32_t) =
        (void (*)(uint32_t, uint32_t, uint32_t, uint32_t))
        (uintptr_t)load32(address(0xf03b33c4u));
    uint32_t channel = ((uint32_t (*)(void))(uintptr_t)load32(address(gp() - 0x217acu)))();
    send(0, channel, 0, buffer.token);
#endif
    return 0;
}

uint32_t rxfilter_firmware_legacy_reply(uint8_t* command, uint32_t status) {
    return reply(address(load32(command + 12)) + 32, (uint8_t)status, 0, 0);
}

static void reset(uint32_t bss) {
#ifdef RXFILTER_FIRMWARE_TEST
    rxfilter_firmware_test_reset(bss);
#else
    rxfilter_original_reset(bss);
#endif
}

static uint32_t seek(uint32_t bss, const uint8_t* mac) {
#ifdef RXFILTER_FIRMWARE_TEST
    return rxfilter_firmware_test_seek(bss, mac);
#else
    return rxfilter_original_seek(bss, mac);
#endif
}

static __attribute__((noinline)) void update(void) {
#ifdef RXFILTER_FIRMWARE_TEST
    rxfilter_firmware_test_clear();
#else
    ((void (*)(void))(uintptr_t)load32(address(0xf03b37f4u)))();
#endif
    for (unsigned int i = 0; i < 32; ++i) {
        uint8_t* entry = controls() + 12 + 8 * i;
        if (!entry[0])
            continue;
#ifdef RXFILTER_FIRMWARE_TEST
        rxfilter_firmware_test_program(i, 0, entry + 2);
#else
        ((void (*)(uint32_t, uint32_t, const uint8_t*))
            (uintptr_t)load32(address(0xf03b37f0u)))(i, 0, entry + 2);
#endif
    }
#ifdef RXFILTER_FIRMWARE_TEST
    if (rxfilter_firmware_test_reconfigure())
        rxfilter_firmware_test_assert(0xf0392560u, 0x45d);
#else
    if (rxfilter_original_reconfigure()) {
        rxfilter_original_assert_log(0xf0392560u, 0x45d);
        ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t))
            (uintptr_t)load32(address(gp() - 0x22c00u)))(0, 0, 0xf0392560u, 0x45d);
    }
#endif
}

uint32_t rxfilter_firmware_packet(const uint8_t* descriptor) {
    if (descriptor[0x28] != 2 || descriptor[0x48] != 1 || descriptor[0x49] ||
        descriptor[0x69] >= 4)
        return 0;
    const uint8_t* state = controls();
    if (!state)
        return 0;
    unsigned int index = descriptor[0x69];
    uint8_t policy = rxfilter_query_control_status(state[index], state[index + 4], (uint8_t)index);
    if (policy == RXFILTER_QUERY_STATUS_INVALID || !(policy & RXFILTER_ENABLED))
        return 0;
    uint32_t raw = load32(descriptor + 0x24);
    uint32_t total = load16(descriptor + 0x2a);
    uint32_t ethernet = load32(descriptor + 0x2c);
    if (!raw || total > UINT32_MAX - raw || ethernet < raw || ethernet - raw > total)
        return 0;
    return rxfilter_packet(policy & 7, address(ethernet), total - (ethernet - raw));
}

extern uint32_t rxfilter_original_multicast(const uint8_t* descriptor);

uint32_t rxfilter_firmware_rx(const uint8_t* descriptor, uint32_t broadcast) {
    uint32_t group = rxfilter_original_multicast(descriptor);
    uint32_t bitmap = rxfilter_legacy_bitmap(descriptor);
    if (!*address(0xf03add1eu))
        return 0;
    broadcast &= 255;
    if (!(broadcast || group || bitmap)) {
        if (!rxfilter_firmware_packet(descriptor))
            return 0;
        group = 1;
    }
    return rxfilter_legacy_record_drop(descriptor, broadcast ? 9 : group ? 10 : 11);
}

uint32_t rxfilter_firmware_command(uint8_t* command) {
    uint8_t* raw = address(load32(command + 0x0c));
    if (!raw)
        return 0xc0000001u;
    const uint8_t* header = raw + 0x20;
    unsigned int total = load16(header);
    if (total < 40)
        return 0xc0000001u;
    const uint8_t* payload = header + 32;
    if (!header[6] && payload[5] == 'R' && payload[6] == 'X') {
        uint8_t status = RXFILTER_QUERY_STATUS_INVALID;
        const uint8_t* state = controls();
        if (total == 44 && !load32(payload) && payload[4] < 4 &&
            payload[7] == RXFILTER_QUERY_VERSION && state)
            status = rxfilter_query_control_status(state[payload[4]], state[payload[4] + 4], payload[4]);
        return reply(header, status, total >= 44 ? load32(payload + 8) : 0, 1);
    }
    uint32_t bss = payload[4];
    if (bss >= 4)
        return 0xc0000001u;
    if (!header[6])
        return 0;
    uint32_t count = load32(payload);
    if (count != 255 && count > (total - 40u) / 6u)
        return 0xc0000001u;
    reset(bss);
    uint8_t* state = controls();
    store32(state - 16 + 4 * bss, count);
    uint32_t failed = count == 255;
    for (uint32_t i = 0; !failed && i < count; ++i)
        failed = !seek(bss, payload + 8 + 6 * i);
    unsigned int mode = payload[5] >> 4;
    uint32_t family = mode >= 8 && mode <= 11 && mode == payload[6] >> 4;
    for (unsigned int i = 0; i < 3; ++i) {
        uint8_t value = payload[5 + i];
        if (family && i < 2) {
            value = (uint8_t)((mode << 4) | 3);
        } else if (failed) {
            value = 0x53;
        } else {
            uint8_t fallback = i ? 0x42 : 0x53;
            if (!(value & 15))
                value |= fallback & 15;
            if (value < 16)
                value |= fallback & 0xf0;
        }
        controls()[bss + 4 * i] = value;
    }
    update();
    return 0;
}
