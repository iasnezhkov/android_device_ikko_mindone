#ifndef MINDONE_RXFILTER_POLICY_H
#define MINDONE_RXFILTER_POLICY_H

#include <stddef.h>
#include <stdint.h>

enum rxfilter_policy_bits {
    RXFILTER_ALLOW_IPV4 = 1,
    RXFILTER_ALLOW_IPV6 = 2,
    RXFILTER_ENABLED = 4
};

enum rxfilter_verdict {
    RXFILTER_INHERIT = 0,
    RXFILTER_DROP = 1
};

enum rxfilter_verdict rxfilter_packet(uint8_t policy, const uint8_t* frame, size_t length);
enum rxfilter_verdict rxfilter_soc2_packet(const uint8_t policies[4], const uint8_t* descriptor,
                                         size_t descriptor_size, const uint8_t* raw,
                                         size_t raw_size, uint32_t raw_address);

#endif
