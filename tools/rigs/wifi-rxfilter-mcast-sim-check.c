#include "../wifi/rxfilter-mcast.h"

typedef uint32_t u32;
typedef uint8_t u8;

static u8 descriptor[112] __attribute__((aligned(4)));
static u8 state[0x204] __attribute__((aligned(4)));
static u8 snapshot[0x204] __attribute__((aligned(4)));
static u8 ethernet[16] __attribute__((aligned(4)));
static u8 ip[40] __attribute__((aligned(4)));
static u8 mapped[6];
static u32 result[12];

static void copy(u8* to, const u8* from, u32 length) {
    while (length--)
        *to++ = *from++;
}

static void clear(u8* to, u32 length) {
    while (length--)
        *to++ = 0;
}

static void put(u8* to, u32 value) {
    *(u32*)to = value;
}

int sim_memcmp(const u8* a, const u8* b, u32 length) {
    while (length--) {
        if (*a != *b)
            return (int)*a - (int)*b;
        ++a;
        ++b;
    }
    return 0;
}

static int compare(void) {
    copy(snapshot, state, sizeof(state));
    u32 original = ((u32 (*)(const u8*))(uintptr_t)0xf032a1b4u)(descriptor);
    u32 replacement = rxfilter_legacy_mcast(descriptor);
    ++result[0];
    if (original != replacement || sim_memcmp(snapshot, state, sizeof(state))) {
        ++result[1];
        result[2] = original;
        result[3] = replacement;
        return 1;
    }
    return 0;
}

static void packet(u32 kind) {
    static const u8 l2[] = {1, 0, 0x5e, 0x11, 0x22, 0x33};
    static const u8 v4[] = {1, 0, 0x5e, 127, 0x34, 0x56};
    static const u8 v6[] = {0x33, 0x33, 0x12, 0x34, 0x56, 0x78};
    copy(ethernet, l2, 6);
    copy(mapped, v4, 6);
    clear(ip, sizeof(ip));
    ethernet[12] = 8;
    ethernet[13] = 0;
    ip[16] = 224;
    ip[17] = 127;
    ip[18] = 0x34;
    ip[19] = 0x56;
    if (kind == 1)
        ip[17] = 128;
    if (kind == 2)
        ip[16] = 239;
    if (kind == 3)
        ip[16] = 192;
    if (kind == 4 || kind == 5) {
        ethernet[12] = 0x86;
        ethernet[13] = 0xdd;
        ip[24] = kind == 4 ? 255 : 254;
        copy(mapped, v6, 6);
        copy(ip + 36, v6 + 2, 4);
    }
    if (kind == 6)
        ethernet[13] = 6;
}

static void entry(u32 slot, u32 mask, u32 active, const u8* mac) {
    u8* target = state + 0x100 + slot * 8;
    target[0] = active;
    target[1] = mask;
    copy(target + 2, mac, 6);
}

static void table(u32 pattern, u32 bss) {
    clear(state + 0x100, 0x100);
    u32 active = pattern == 4 ? 0 : pattern == 7 ? 0x80 : 1;
    u32 mask = pattern == 5 ? (1u << ((bss + 1) % 4)) :
               pattern == 6 ? 255 : (1u << bss);
    if (pattern == 1 || pattern >= 3)
        entry(0, mask, active, ethernet);
    if (pattern == 2 || pattern >= 3)
        entry(31, mask, active, mapped);
}

static void controls(u32 mode, u32 bss, u32 sleep, u32 flags) {
    for (u32 i = 0; i < 8; ++i)
        state[0xf4 + i] = 0x13;
    state[(sleep ? 0xf8 : 0xf4) + bss] = mode;
    *(u8*)(uintptr_t)0xf03add1eu = sleep ? 0x80 : 0;
    descriptor[0x69] = bss;
    descriptor[0x3c] = flags & 1 ? 2 : 0;
    descriptor[0x3d] = flags & 4 ? 0x80 : 0;
    put(descriptor + 0x64, flags & 2 ? 0x5000 : 0);
    result[5] = bss;
    result[6] = mode;
    result[7] = sleep;
    result[9] = flags;
}

int main(void) {
    put((u8*)(uintptr_t)0xf03adc2cu, (u32)(uintptr_t)state);
    put(descriptor + 0x2c, (u32)(uintptr_t)ethernet);
    put(descriptor + 0x30, (u32)(uintptr_t)ip);
    for (u32 kind = 0; kind < 7; ++kind) {
        packet(kind);
        result[4] = kind;
        for (u32 bss = 0; bss < 4; bss += 3) {
            for (u32 pattern = 0; pattern < 8; ++pattern) {
                table(pattern, bss);
                result[8] = pattern;
                for (u32 mode = 0; mode < 256; ++mode) {
                    for (u32 sleep = 0; sleep < 2; ++sleep) {
                        controls(mode, bss, sleep, 3);
                        if (compare())
                            goto done;
                    }
                }
            }
        }
    }
    static const u8 modes[] = {0x11, 0x12, 0x13, 0x21, 0x22, 0x23,
                              0x31, 0x32, 0x33, 0x41, 0x42, 0x43, 0x53, 0x83, 0xff};
    for (u32 kind = 0; kind < 7; ++kind) {
        packet(kind);
        result[4] = kind;
        for (u32 bss = 0; bss < 4; ++bss) {
            for (u32 pattern = 0; pattern < 8; ++pattern) {
                table(pattern, bss);
                result[8] = pattern;
                for (u32 i = 0; i < sizeof(modes); ++i) {
                    for (u32 flags = 0; flags < 8; ++flags) {
                        for (u32 sleep = 0; sleep < 2; ++sleep) {
                            controls(modes[i], bss, sleep, flags);
                            if (compare())
                                goto done;
                        }
                    }
                }
            }
        }
    }
done:;
    register u32 r0 __asm__("$r0") = 1;
    register void* r1 __asm__("$r1") = result;
    register u32 r2 __asm__("$r2") = sizeof(result);
    __asm__ volatile("syscall 5" : "+r"(r0) : "r"(r1), "r"(r2) : "memory");
    return result[1] != 0;
}
