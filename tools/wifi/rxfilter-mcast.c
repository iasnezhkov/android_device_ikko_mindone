#include "rxfilter-mcast.h"

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

static const uint8_t* address(uint32_t value) {
#ifdef RXFILTER_MCAST_TEST
    return rxfilter_mcast_test_address(value);
#else
    return (const uint8_t*)(uintptr_t)value;
#endif
}

static int equal_mac(const uint8_t* a, const uint8_t* b) {
    for (unsigned int i = 0; i < 6; ++i) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

uint32_t rxfilter_legacy_mcast(const uint8_t* descriptor) {
    if (descriptor[0x3d])
        return 0;
    uint8_t multicast = descriptor[0x3c] != 0;
    if (!load32(descriptor + 0x64) && multicast)
        return 1;
    uint8_t bss = descriptor[0x69];
    if (bss >= 4)
        return 0;
    const uint8_t* state = address(load32(address(0xf03adc2cu)));
    uint8_t mode = state[(*address(0xf03add1eu) ? 0xf8 : 0xf4) + bss];
    if ((mode & 15) == 1 && multicast)
        return 1;
    const uint8_t* ethernet = address(load32(descriptor + 0x2c));
    const uint8_t* ip = address(load32(descriptor + 0x30));
    uint16_t type = load16(ethernet + 12);
    uint8_t mapped[6];
    uint8_t family;
    if (type == 0x0008 && ip[16] == 224 && ip[17] < 128) {
        family = 2;
        mapped[0] = 1;
        mapped[1] = 0;
        mapped[2] = 0x5e;
        mapped[3] = ip[17];
        mapped[4] = ip[18];
        mapped[5] = ip[19];
    } else if (type == 0xdd86 && ip[24] == 255) {
        family = 3;
        mapped[0] = 0x33;
        mapped[1] = 0x33;
        for (unsigned int i = 2; i < 6; ++i)
            mapped[i] = ip[34 + i];
    } else {
        return 0;
    }
    uint8_t l2_match = 0;
    uint8_t ip_match = 0;
    for (unsigned int i = 0; i < 32; ++i) {
        const uint8_t* entry = state + 0x100 + 8 * i;
        if (entry[0] && (entry[1] & (1u << bss))) {
            l2_match |= multicast && equal_mac(entry + 2, ethernet);
            ip_match |= equal_mac(entry + 2, mapped);
        }
    }
    if ((mode & 15) == 2 && !l2_match)
        return 1;
    uint8_t high = mode >> 4;
    if (high == 1)
        return 1;
    if (high >= 2 && high <= 4)
        return !ip_match || (high != 4 && high != family);
    return 0;
}
