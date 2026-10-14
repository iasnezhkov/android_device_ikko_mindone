#include "rxfilter-legacy.h"

#include <stddef.h>

#ifndef RXFILTER_LEGACY_TEST
extern uint32_t rxfilter_original_count(uint32_t family);
extern uint32_t rxfilter_original_multicast(const uint8_t* descriptor);
extern void rxfilter_original_reason(uint32_t reason);
#endif

static uint32_t load32(const uint8_t* p) {
    uint32_t value;
    __builtin_memcpy(&value, __builtin_assume_aligned(p, 4), sizeof(value));
    return value;
}

static uint16_t load16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint16_t network16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static void store32(uint8_t* p, uint32_t value) {
    __builtin_memcpy(__builtin_assume_aligned(p, 4), &value, sizeof(value));
}

static uint8_t* address(uint32_t value) {
#ifdef RXFILTER_LEGACY_TEST
    return rxfilter_legacy_test_address(value);
#else
    return (uint8_t*)(uintptr_t)value;
#endif
}

static uint8_t* pointer(const uint8_t* p) {
    return address(load32(p));
}

static uint32_t count_addresses(uint32_t family) {
#ifdef RXFILTER_LEGACY_TEST
    return rxfilter_legacy_test_count(family);
#else
    return rxfilter_original_count(family);
#endif
}

static uint32_t multicast(const uint8_t* descriptor) {
#ifdef RXFILTER_LEGACY_TEST
    return rxfilter_legacy_test_multicast(descriptor);
#else
    return rxfilter_original_multicast(descriptor);
#endif
}

static void drop_reason(uint32_t reason) {
#ifdef RXFILTER_LEGACY_TEST
    rxfilter_legacy_test_reason(reason);
#else
    rxfilter_original_reason(reason);
#endif
}

static int equal(const uint8_t* a, const uint8_t* b, unsigned int length) {
    while (length--) {
        if (*a++ != *b++)
            return 0;
    }
    return 1;
}

static uint32_t ipv4_udp(uint32_t mask, const uint8_t* ip) {
    if (mask & 1)
        return 1;
    unsigned int source = network16(ip + 20);
    unsigned int destination = network16(ip + 22);
    if (source == 67 && destination == 68)
        return 0;
    static const uint16_t ports[] = {5353, 5355, 138, 5246, 53, 137, 1900};
    if (source == 68 && destination == 67)
        return (mask >> 10) & 1;
    for (unsigned int i = 0; i < 7; ++i)
        if (source == ports[i] || destination == ports[i])
            return (mask >> (i + 1)) & 1;
    if (destination == 19132)
        return (mask >> 11) & 1;
    if ((mask & (1u << 12)) && ip[16] == 224 && !ip[17] && !ip[18])
        return 1;
    return (mask >> 8) & 1;
}

uint32_t rxfilter_legacy_bitmap(const uint8_t* descriptor) {
    const uint8_t* station = pointer(descriptor + 0x64);
    if (!station)
        return 0;
    const uint8_t* bss = pointer(station + 0x0c);
    if (!bss || bss[0] >= 4 || load32(bss + 0x28) || load32(bss + 0x1c) ||
        (!count_addresses(0) && !count_addresses(1)))
        return 0;
    const uint8_t* ethernet = pointer(descriptor + 0x2c);
    const uint8_t* ip = pointer(descriptor + 0x30);
    const uint8_t* list = pointer(address(0xf03c65c8u) + 4u * bss[0]);
    const uint8_t* masks = address(0xf03c6410u);
    unsigned int type = network16(ethernet + 12);
    if (type == 0x0800) {
        uint32_t mask = load32(masks);
        if (!(mask | load32(masks + 4)))
            return 0;
        for (unsigned int i = 0; i < list[0x20]; ++i) {
            if (equal(ip + 16, list + i * 8, 4))
                return 0;
        }
        if (ip[9] == 17)
            return ipv4_udp(mask, ip);
        return ip[9] == 2 ? (mask >> 9) & 1 : 0;
    }
    if (type == 0x86dd) {
        uint32_t mask = load32(masks + 8);
        if (!(mask | load32(masks + 12)))
            return 0;
        for (unsigned int i = 0; i < list[0x65]; ++i) {
            if (equal(ip + 24, list + 0x25 + i * 16, 16))
                return 0;
        }
        return (mask & 1) || ((mask & 2) && ip[24] == 255);
    }
    uint32_t mask = load32(masks + 16);
    if (mask & 1)
        return 1;
    unsigned int bit;
    if (type == 0x9000)
        bit = 5;
    else if (type == 0x2000)
        bit = 1;
    else if (load16(ethernet + 12) < 8)
        bit = 4;
    else
        return 0;
    return (mask >> bit) & 1;
}

uint32_t rxfilter_legacy_record_drop(const uint8_t* descriptor, uint32_t reason) {
    const uint8_t* station = pointer(descriptor + 0x64);
    if (station) {
        const uint8_t* bss = pointer(station + 0x0c);
        if (!bss || bss[0] >= 4)
            return 0;
        uint8_t* statistics = pointer(bss + 0x38);
        store32(statistics + 0x2a8, load32(statistics + 0x2a8) + 1);
        store32(statistics + 0x310, load32(statistics + 0x308) + 1);
    }
    uint8_t* global = pointer(address(0xf03adc2cu));
    uint32_t low = load32(global + 0x58) + 1;
    store32(global + 0x58, low);
    store32(global + 0x5c, load32(global + 0x5c) + (low == 0));
    drop_reason(reason);
    return 1;
}

uint32_t rxfilter_legacy_wrapper(const uint8_t* descriptor, uint32_t broadcast) {
    broadcast &= 255;
    uint32_t group = multicast(descriptor);
    uint32_t bitmap = rxfilter_legacy_bitmap(descriptor);
    if (!*address(0xf03add1eu) || !(broadcast || group || bitmap))
        return 0;
    return rxfilter_legacy_record_drop(descriptor, broadcast ? 9 : group ? 10 : 11);
}
