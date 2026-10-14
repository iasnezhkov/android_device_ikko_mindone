#include "../wifi/rxfilter-table.h"

#ifdef RXFILTER_TABLE_TEST
#include <stdio.h>
#endif

typedef uint8_t u8;
typedef uint32_t u32;

u8 rxfilter_table_state[0x204] __attribute__((aligned(4)));
static u8 reference[0x204] __attribute__((aligned(4)));
static u8 address[6];
static u32 result[12];

void* sim_memcpy(void* destination, const void* source, u32 size) {
    u8* d = destination;
    const u8* s = source;
    for (u32 i = 0; i < size; ++i)
        d[i] = s[i];
    return destination;
}

int sim_memcmp(const void* first, const void* second, u32 size) {
    const u8* a = first;
    const u8* b = second;
    for (u32 i = 0; i < size; ++i)
        if (a[i] != b[i])
            return (int)a[i] - b[i];
    return 0;
}

#ifdef RXFILTER_TABLE_TEST
static void original_reset(u8 bss) {
    for (u32 i = 0; i < 4; ++i)
        reference[0xe4 + bss * 4 + i] = 0;
    reference[0xf4 + bss] = 0x53;
    reference[0xf8 + bss] = 0x42;
    reference[0xfc + bss] = 0x42;
    for (u32 i = 0x100; i < 0x200; i += 8) {
        if (!reference[i] || !(reference[i + 1] & (1u << bss)))
            continue;
        reference[i + 1] = (u8)(reference[i + 1] & ~(1u << bss));
        if (!reference[i + 1])
            reference[i] = 0;
    }
}

static int original_seek(u8 bss, const u8* mac) {
    u32 index;
    for (index = 0; index != 32; ++index) {
        u8* entry = reference + 0x100 + index * 8;
        if (!entry[0]) {
            sim_memcpy(entry + 2, mac, 6);
            entry[1] = (u8)(entry[1] | (1u << bss));
            entry[0] = 1;
            break;
        }
        if (!sim_memcmp(mac, entry + 2, 6)) {
            if (!(entry[1] & (1u << bss)))
                entry[1] = (u8)(entry[1] | (1u << bss));
            break;
        }
    }
    return index != 32;
}
#else
static void original_reset(u8 bss) {
    *(u8* volatile*)(uintptr_t)0xf03adc2c = reference;
    ((void (*)(u8))(uintptr_t)0xf032aa64)(bss);
}

static int original_seek(u8 bss, const u8* mac) {
    *(u8* volatile*)(uintptr_t)0xf03adc2c = reference;
    return ((int (*)(u8, const u8*))(uintptr_t)0xf032b564)(bss, mac);
}
#endif

static int compare(u32 bss, int reset) {
    sim_memcpy(reference, rxfilter_table_state, sizeof(reference));
    int expected = 0;
    int actual = 0;
    if (reset)
        original_reset((u8)bss);
    else
        expected = original_seek((u8)bss, address);
#ifndef RXFILTER_TABLE_TEST
    *(u8* volatile*)(uintptr_t)0xf03adc2c = rxfilter_table_state;
#endif
    if (reset)
        rxfilter_legacy_table_reset((u8)bss);
    else
        actual = rxfilter_legacy_table_seek((u8)bss, address);
    ++result[0];
    if (actual != expected || sim_memcmp(reference, rxfilter_table_state, sizeof(reference))) {
        ++result[1];
        result[2] = expected;
        result[3] = actual;
        result[4] = bss;
        result[5] = reset;
        for (u32 i = 0; i < sizeof(reference); ++i) {
            if (reference[i] != rxfilter_table_state[i]) {
                result[9] = i;
                result[10] = reference[i];
                result[11] = rxfilter_table_state[i];
                break;
            }
        }
        return 1;
    }
    return 0;
}

static void prepare(u32 seed) {
    for (u32 i = 0; i < sizeof(rxfilter_table_state); ++i)
        rxfilter_table_state[i] = (u8)(i * 19 + seed);
    for (u32 i = 0; i < 6; ++i)
        address[i] = (u8)(0x31 + i * 17);
    for (u32 i = 0; i < 32; ++i) {
        u8* entry = rxfilter_table_state + 0x100 + i * 8;
        entry[0] = 0x80;
        for (u32 j = 0; j < 6; ++j)
            entry[j + 2] = (u8)(address[j] ^ 0x55);
    }
}

int main(void) {
    for (u32 bss = 0; bss < 4; ++bss) {
        for (u32 slot = 0; slot < 32; ++slot) {
            result[6] = slot;
            for (u32 value = 0; value < 256; ++value) {
                result[7] = value;
                prepare(value);
                u8* entry = rxfilter_table_state + 0x100 + slot * 8;
                entry[0] = value;
                entry[1] = value;
                if (compare(bss, 1) || compare(bss, 1))
                    goto done;
                prepare(value);
                entry[0] = 0;
                entry[1] = value;
                if (slot < 31)
                    sim_memcpy(entry + 10, address, 6);
                if (compare(bss, 0) || compare(bss, 0))
                    goto done;
                prepare(value);
                sim_memcpy(entry + 2, address, 6);
                entry[0] = value;
                entry[1] = value;
                if (compare(bss, 0))
                    goto done;
            }
            for (u32 byte = 0; byte < 6; ++byte) {
                result[8] = byte;
                for (u32 value = 0; value < 256; ++value) {
                    prepare(value);
                    u8* entry = rxfilter_table_state + 0x100 + slot * 8;
                    sim_memcpy(entry + 2, address, 6);
                    entry[byte + 2] = value;
                    if (compare(bss, 0))
                        goto done;
                }
            }
        }
        prepare(bss);
        for (u32 i = 0; i < 32; ++i)
            rxfilter_table_state[0x100 + i * 8] = 0;
        for (u32 i = 0; i < 33; ++i) {
            address[5] = i;
            if (compare(bss, 0))
                goto done;
        }
        if (compare(bss, 1))
            goto done;
    }
done:
#ifdef RXFILTER_TABLE_TEST
    printf("%u checks, %u failures, operation=%u bss=%u offset=%u expected=%u actual=%u\n",
           result[0], result[1], result[5], result[4], result[9], result[10], result[11]);
#else
    ;
    register u32 r0 __asm__("$r0") = 1;
    register void* r1 __asm__("$r1") = result;
    register u32 r2 __asm__("$r2") = sizeof(result);
    __asm__ volatile("syscall 5" : "+r"(r0) : "r"(r1), "r"(r2) : "memory");
#endif
    return result[1] != 0;
}
