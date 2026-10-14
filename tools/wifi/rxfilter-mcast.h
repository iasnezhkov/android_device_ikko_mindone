#ifndef MINDONE_RXFILTER_MCAST_H
#define MINDONE_RXFILTER_MCAST_H

#include <stdint.h>

uint32_t rxfilter_legacy_mcast(const uint8_t* descriptor);

#ifdef RXFILTER_MCAST_TEST
void* rxfilter_mcast_test_address(uint32_t address);
#endif

#endif
