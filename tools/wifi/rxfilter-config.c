#include "rxfilter-config.h"

static uint32_t load32(const uint8_t* bytes) {
    uint32_t value;
    __builtin_memcpy(&value, __builtin_assume_aligned(bytes, 4), sizeof(value));
    return value;
}

uint32_t rxfilter_legacy_set_config(const uint8_t* command) {
    if (!command)
        return 0;
    const uint8_t* raw = (const uint8_t*)(uintptr_t)load32(command + 12);
    if (raw[0x42] != 0x72 || raw[0x43] >= 25)
        return 0;
    uint32_t* masks = (uint32_t*)(uintptr_t)0xf03c6410u;
    for (unsigned int i = 0; i < 6; ++i)
        masks[i] = load32(raw + 0x44 + i * 4);
    return 0;
}

uint32_t rxfilter_legacy_init_config(void) {
    uint32_t* masks = (uint32_t*)(uintptr_t)0xf03c6410u;
    for (unsigned int i = 0; i < 6; ++i)
        masks[i] = 0;
    ((uint8_t*)masks)[24] = 0;
    return 0;
}
