#include "rxfilter-query.h"

uint8_t rxfilter_query_control_status(uint8_t awake, uint8_t asleep, uint8_t bss) {
    uint8_t policy = 0;
    if (awake == asleep && (awake & 0xcf) == 0x83)
        policy = (uint8_t)((awake >> 4) - 4);
    else if ((uint8_t)((awake & 15) - 1) > 2 ||
             (uint8_t)((asleep & 15) - 1) > 2 ||
             (uint8_t)((awake >> 4) - 1) > 4 ||
             (uint8_t)((asleep >> 4) - 1) > 4)
        return RXFILTER_QUERY_STATUS_INVALID;
    return (uint8_t)(RXFILTER_QUERY_STATUS_BASE | (bss << 3) | policy);
}

static int valid_policy(uint8_t policy) {
    return policy == 0 || (policy >= 4 && policy <= 7);
}

enum rxfilter_query_action rxfilter_query_status(uint8_t set_query,
    const uint8_t* request, size_t request_size, const uint8_t* controls,
    size_t controls_size, uint8_t* status) {
    if (set_query || !request || request_size < 7 ||
        request[5] != 'R' || request[6] != 'X' || !status)
        return RXFILTER_QUERY_IGNORE;
    *status = RXFILTER_QUERY_STATUS_INVALID;
    if (request_size != 12 || request[0] || request[1] || request[2] || request[3] ||
        request[4] >= 4 || request[7] != RXFILTER_QUERY_VERSION ||
        !controls || controls_size != 8)
        return RXFILTER_QUERY_REPLY;
    uint8_t bss = request[4];
    *status = rxfilter_query_control_status(controls[bss], controls[bss + 4], bss);
    return RXFILTER_QUERY_REPLY;
}

int rxfilter_query_response_matches(uint8_t eid, uint8_t sequence,
    const uint8_t* payload, size_t payload_size, uint8_t expected_sequence,
    uint8_t expected_bss, uint8_t expected_policy, uint32_t expected_nonce) {
    if (eid != RXFILTER_QUERY_EID || sequence != expected_sequence || !payload ||
        payload_size != 12 || expected_bss >= 4 || !valid_policy(expected_policy) ||
        payload[1] != RXFILTER_QUERY_CID || payload[2] != RXFILTER_QUERY_VERSION ||
        payload[3] || payload[11])
        return 0;
    uint32_t nonce = (uint32_t)payload[4] | (uint32_t)payload[5] << 8 |
                     (uint32_t)payload[6] << 16 | (uint32_t)payload[7] << 24;
    return nonce == expected_nonce && payload[0] == (uint8_t)(RXFILTER_QUERY_STATUS_BASE |
                                                            (expected_bss << 3) | expected_policy) &&
           rxfilter_query_control_status(payload[8], payload[9], expected_bss) == payload[0];
}
