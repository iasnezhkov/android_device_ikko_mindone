#ifndef RXFILTER_TABLE_H
#define RXFILTER_TABLE_H

#include <stdint.h>

void rxfilter_legacy_table_reset(uint8_t bss);
int rxfilter_legacy_table_seek(uint8_t bss, const uint8_t* address);

#endif
