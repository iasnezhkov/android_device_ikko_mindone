#ifndef MINDONE_RXFILTER_LEGACY_H
#define MINDONE_RXFILTER_LEGACY_H

#include <stdint.h>

uint32_t rxfilter_legacy_bitmap(const uint8_t* descriptor);
uint32_t rxfilter_legacy_wrapper(const uint8_t* descriptor, uint32_t broadcast);
uint32_t rxfilter_legacy_record_drop(const uint8_t* descriptor, uint32_t reason);

#ifdef RXFILTER_LEGACY_TEST
void* rxfilter_legacy_test_address(uint32_t address);
uint32_t rxfilter_legacy_test_count(uint32_t family);
uint32_t rxfilter_legacy_test_multicast(const uint8_t* descriptor);
void rxfilter_legacy_test_reason(uint32_t reason);
#endif

#endif
