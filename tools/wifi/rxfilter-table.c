#include "rxfilter-table.h"

static uint8_t* table_state(void) {
#ifdef RXFILTER_TABLE_TEST
    extern uint8_t rxfilter_table_state[0x204];
    return rxfilter_table_state;
#else
    return *(uint8_t* volatile*)(uintptr_t)0xf03adc2c;
#endif
}

void rxfilter_legacy_table_reset(uint8_t bss) {
    uint8_t* state = table_state();
    *(uint32_t*)(void*)(state + 0xe4 + 4u * bss) = 0;
    state[0xf4 + bss] = 0x53;
    state[0xf8 + bss] = 0x42;
    state[0xfc + bss] = 0x42;
    unsigned int mask = 1u << bss;
    for (unsigned i = 0; i < 32; ++i) {
        uint8_t* entry = state + 0x100 + i * 8;
        if (entry[0] && (entry[1] & mask)) {
            entry[1] &= (uint8_t)~mask;
            if (!entry[1])
                entry[0] = 0;
        }
    }
}

int rxfilter_legacy_table_seek(uint8_t bss, const uint8_t* address) {
    uint8_t* state = table_state();
    for (unsigned i = 0; i < 32; ++i) {
        uint8_t* entry = state + 0x100 + i * 8;
        unsigned j = 0;
        if (entry[0]) {
            while (j < 6 && entry[j + 2] == address[j])
                ++j;
            if (j != 6)
                continue;
        } else {
            for (j = 0; j < 6; ++j)
                entry[j + 2] = address[j];
            entry[1] |= (uint8_t)(1u << bss);
            entry[0] = 1;
            return 1;
        }
        entry[1] |= (uint8_t)(1u << bss);
        return 1;
    }
    return 0;
}
