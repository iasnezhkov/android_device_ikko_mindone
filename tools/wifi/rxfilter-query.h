#ifndef MINDONE_RXFILTER_QUERY_H
#define MINDONE_RXFILTER_QUERY_H

#include <stddef.h>
#include <stdint.h>

uint8_t rxfilter_query_control_status(uint8_t awake, uint8_t asleep, uint8_t bss);

enum rxfilter_query_action {
    RXFILTER_QUERY_IGNORE = 0,
    RXFILTER_QUERY_REPLY = 1
};

enum rxfilter_query_wire {
    RXFILTER_QUERY_VERSION = 1,
    RXFILTER_QUERY_STATUS_BASE = 0xa0,
    RXFILTER_QUERY_STATUS_INVALID = 0xfc,
    RXFILTER_QUERY_CID = 0xc1,
    RXFILTER_QUERY_EID = 0xfd
};

enum rxfilter_query_action rxfilter_query_status(uint8_t set_query,
    const uint8_t* request, size_t request_size, const uint8_t* controls,
    size_t controls_size, uint8_t* status);

int rxfilter_query_response_matches(uint8_t eid, uint8_t sequence,
    const uint8_t* payload, size_t payload_size, uint8_t expected_sequence,
    uint8_t expected_bss, uint8_t expected_policy, uint32_t expected_nonce);

#endif
