#include "rxfilter-policy.h"

static uint16_t read_be16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
}

static enum rxfilter_verdict ipv4_packet(const uint8_t* ip, size_t length) {
    if (length < 20 || ip[0] >> 4 != 4 || ip[0] % 16 < 5 || ip[16] >> 4 != 14)
        return RXFILTER_INHERIT;
    size_t header = (size_t)(ip[0] % 16) * 4;
    size_t total = read_be16(ip + 2);
    if (header > length || total < header || total > length ||
        (read_be16(ip + 6) & 0x3fff) || ip[9] == 2)
        return RXFILTER_INHERIT;
    if (ip[9] == 17) {
        if (total - header < 8)
            return RXFILTER_INHERIT;
        const uint8_t* udp = ip + header;
        size_t udp_size = read_be16(udp + 4);
        if (udp_size < 8 || udp_size > total - header)
            return RXFILTER_INHERIT;
        uint16_t source = read_be16(udp);
        uint16_t destination = read_be16(udp + 2);
        if ((source == 67 && destination == 68) ||
            (source == 68 && destination == 67))
            return RXFILTER_INHERIT;
    }
    return RXFILTER_DROP;
}

static enum rxfilter_verdict ipv6_packet(const uint8_t* ip, size_t length) {
    if (length < 40 || ip[0] >> 4 != 6 || ip[24] != 255)
        return RXFILTER_INHERIT;
    size_t payload = read_be16(ip + 4);
    if (payload == 0 || payload > length - 40)
        return RXFILTER_INHERIT;
    size_t remaining = payload;
    const uint8_t* next = ip + 40;
    uint8_t protocol = ip[6];
    for (unsigned int headers = 0; headers < 8; ++headers) {
        if (protocol == 58 || protocol == 44 || protocol == 50)
            return RXFILTER_INHERIT;
        if (protocol == 0 || protocol == 43 || protocol == 60 || protocol == 51) {
            if (remaining < 2)
                return RXFILTER_INHERIT;
            size_t header = protocol == 51 ? ((size_t)next[1] + 2) * 4
                                          : ((size_t)next[1] + 1) * 8;
            if (header > remaining)
                return RXFILTER_INHERIT;
            protocol = next[0];
            next += header;
            remaining -= header;
            continue;
        }
        if (protocol == 17) {
            if (remaining < 8)
                return RXFILTER_INHERIT;
            size_t udp_size = read_be16(next + 4);
            if (udp_size < 8 || udp_size > remaining)
                return RXFILTER_INHERIT;
            uint16_t source = read_be16(next);
            uint16_t destination = read_be16(next + 2);
            if ((source == 547 && destination == 546) ||
                (source == 546 && destination == 547) ||
                (source == 547 && destination == 547))
                return RXFILTER_INHERIT;
        }
        return RXFILTER_DROP;
    }
    return RXFILTER_INHERIT;
}

enum rxfilter_verdict rxfilter_packet(uint8_t policy, const uint8_t* frame, size_t length) {
    if ((policy & ~7u) || !(policy & RXFILTER_ENABLED) || !frame || length < 14 ||
        !(frame[0] & 1))
        return RXFILTER_INHERIT;
    size_t header = 14;
    uint16_t protocol = read_be16(frame + 12);
    for (unsigned int tags = 0; tags < 2; ++tags) {
        if (protocol != 0x8100 && protocol != 0x88a8)
            break;
        if (length - header < 4)
            return RXFILTER_INHERIT;
        protocol = read_be16(frame + header + 2);
        header += 4;
    }
    if (protocol == 0x0800 && !(policy & RXFILTER_ALLOW_IPV4))
        return ipv4_packet(frame + header, length - header);
    if (protocol == 0x86dd && !(policy & RXFILTER_ALLOW_IPV6))
        return ipv6_packet(frame + header, length - header);
    return RXFILTER_INHERIT;
}

static uint16_t read_le16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[1] << 8 | bytes[0]);
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)read_le16(bytes) | (uint32_t)read_le16(bytes + 2) << 16;
}

enum rxfilter_verdict rxfilter_soc2_packet(const uint8_t policies[4], const uint8_t* descriptor,
                                         size_t descriptor_size, const uint8_t* raw,
                                         size_t raw_size, uint32_t raw_address) {
    if (!policies || !descriptor || descriptor_size < 0x6a || !raw ||
        descriptor[0x28] != 2 || descriptor[0x48] != 1 || descriptor[0x49] != 0 ||
        descriptor[0x69] >= 4 || read_le32(descriptor + 0x24) != raw_address)
        return RXFILTER_INHERIT;
    uint32_t total = read_le16(descriptor + 0x2a);
    uint32_t layer2 = read_le32(descriptor + 0x2c);
    uint32_t layer3 = read_le32(descriptor + 0x30);
    if (total > raw_size || total > UINT32_MAX - raw_address ||
        layer2 < raw_address || layer3 < layer2)
        return RXFILTER_INHERIT;
    uint32_t offset2 = layer2 - raw_address;
    uint32_t offset3 = layer3 - raw_address;
    if (offset2 > total || offset3 > total || offset3 - offset2 < 14 ||
        total - offset3 != read_le16(descriptor + 0x34))
        return RXFILTER_INHERIT;
    return rxfilter_packet(policies[descriptor[0x69]], raw + offset2, total - offset2);
}
