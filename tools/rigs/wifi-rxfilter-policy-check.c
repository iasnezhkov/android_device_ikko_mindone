#include "../wifi/rxfilter-policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases;
static unsigned failures;

static void expect(int condition, const char* name) {
    ++cases;
    if (!condition) {
        ++failures;
        printf("FAIL %s\n", name);
    }
}

static void be16(uint8_t* target, unsigned value) {
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static size_t ipv4(uint8_t* frame) {
    memset(frame, 0, 256);
    frame[0] = 1;
    frame[2] = 0x5e;
    be16(frame + 12, 0x0800);
    frame[14] = 0x45;
    be16(frame + 16, 28);
    frame[23] = 17;
    frame[30] = 239;
    be16(frame + 34, 5353);
    be16(frame + 36, 5353);
    be16(frame + 38, 8);
    return 42;
}

static size_t ipv6(uint8_t* frame) {
    memset(frame, 0, 256);
    frame[0] = frame[1] = 0x33;
    be16(frame + 12, 0x86dd);
    frame[14] = 0x60;
    be16(frame + 18, 8);
    frame[20] = 17;
    frame[38] = 255;
    frame[39] = 2;
    be16(frame + 54, 5353);
    be16(frame + 56, 5353);
    be16(frame + 58, 8);
    return 62;
}

static void protected_packet(uint8_t* frame, size_t length, const char* name) {
    for (uint8_t policy = 0; policy < 8; ++policy)
        expect(rxfilter_packet(policy, frame, length) == RXFILTER_INHERIT, name);
}

static void le32(uint8_t* target, uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i)
        target[i] = (uint8_t)(value >> (i * 8));
}

static void descriptor_checks(void) {
    const uint32_t address = 0x12345600;
    const uint8_t policies[4] = {4, 5, 6, 7};
    uint8_t raw[300] = {0};
    uint8_t descriptor[0x6a] = {0};
    size_t length = ipv4(raw + 22);
    le32(descriptor + 0x24, address);
    descriptor[0x28] = 2;
    descriptor[0x2a] = (uint8_t)(length + 22);
    le32(descriptor + 0x2c, address + 22);
    le32(descriptor + 0x30, address + 36);
    descriptor[0x34] = (uint8_t)(length - 14);
    descriptor[0x48] = 1;
    for (unsigned int bss = 0; bss < 4; ++bss) {
        descriptor[0x69] = (uint8_t)bss;
        expect(rxfilter_soc2_packet(policies, descriptor, sizeof(descriptor), raw,
                                   length + 22, address) ==
               (bss % 2 ? RXFILTER_INHERIT : RXFILTER_DROP), "firmware BSS selection");
    }
    descriptor[0x69] = 0;
    for (size_t size = 0; size < sizeof(descriptor); ++size) {
        uint8_t* exact = malloc(size ? size : 1);
        memcpy(exact, descriptor, size);
        expect(rxfilter_soc2_packet(policies, exact, size, raw, sizeof(raw), address) ==
               RXFILTER_INHERIT, "truncated firmware descriptor");
        free(exact);
    }
    for (size_t size = 0; size < length + 22; ++size) {
        uint8_t* exact = malloc(size ? size : 1);
        memcpy(exact, raw, size);
        expect(rxfilter_soc2_packet(policies, descriptor, sizeof(descriptor), exact, size, address) ==
               RXFILTER_INHERIT, "truncated firmware RX buffer");
        free(exact);
    }
    const unsigned int flags[] = {0x28, 0x48, 0x49, 0x69, 0x24, 0x34};
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        uint8_t saved = descriptor[flags[i]];
        descriptor[flags[i]] = 255;
        expect(rxfilter_soc2_packet(policies, descriptor, sizeof(descriptor), raw, sizeof(raw), address) ==
               RXFILTER_INHERIT, "invalid descriptor field");
        descriptor[flags[i]] = saved;
    }
    const uint32_t bad_addresses[] = {0, address - 1, address + 300, UINT32_MAX};
    for (unsigned int field = 0x2c; field <= 0x30; field += 4) {
        uint8_t saved[4];
        memcpy(saved, descriptor + field, 4);
        for (size_t i = 0; i < sizeof(bad_addresses) / sizeof(bad_addresses[0]); ++i) {
            le32(descriptor + field, bad_addresses[i]);
            expect(rxfilter_soc2_packet(policies, descriptor, sizeof(descriptor), raw, sizeof(raw), address) ==
                   RXFILTER_INHERIT, "out-of-range firmware pointer");
        }
        memcpy(descriptor + field, saved, 4);
    }
    le32(descriptor + 0x24, UINT32_MAX - 32);
    expect(rxfilter_soc2_packet(policies, descriptor, sizeof(descriptor), raw, sizeof(raw), UINT32_MAX - 32) ==
           RXFILTER_INHERIT, "firmware end address overflow");
    expect(rxfilter_soc2_packet(NULL, descriptor, sizeof(descriptor), raw, sizeof(raw), address) ==
           RXFILTER_INHERIT, "null firmware policy");
    expect(rxfilter_soc2_packet(policies, NULL, sizeof(descriptor), raw, sizeof(raw), address) ==
           RXFILTER_INHERIT, "null firmware descriptor");
    expect(rxfilter_soc2_packet(policies, descriptor, sizeof(descriptor), NULL, sizeof(raw), address) ==
           RXFILTER_INHERIT, "null firmware buffer");
}

int main(void) {
    descriptor_checks();
    uint8_t packet[256];
    for (uint8_t policy = 0; policy < 8; ++policy) {
        size_t length = ipv4(packet);
        for (unsigned group = 224; group < 240; ++group) {
            packet[30] = (uint8_t)group;
            expect(rxfilter_packet(policy, packet, length) ==
                   ((policy & RXFILTER_ENABLED) && !(policy & RXFILTER_ALLOW_IPV4) ? RXFILTER_DROP : RXFILTER_INHERIT), "IPv4 family policy");
        }
        length = ipv6(packet);
        expect(rxfilter_packet(policy, packet, length) ==
               ((policy & RXFILTER_ENABLED) && !(policy & RXFILTER_ALLOW_IPV6) ? RXFILTER_DROP : RXFILTER_INHERIT), "IPv6 family policy");
    }
    size_t length = ipv4(packet);
    for (unsigned policy = 8; policy < 256; ++policy)
        expect(rxfilter_packet((uint8_t)policy, packet, length) == RXFILTER_INHERIT, "unknown policy fail-open");
    const uint8_t policies[4] = {4, 5, 6, 7};
    for (unsigned turn = 0; turn < 32; ++turn)
        for (unsigned bss = 0; bss < 4; ++bss)
            expect(rxfilter_packet(policies[bss], packet, length) == (bss % 2 ? RXFILTER_INHERIT : RXFILTER_DROP), "independent BSS input policies");
    packet[0] = 2;
    protected_packet(packet, length, "unicast push");
    length = ipv4(packet);
    memset(packet, 255, 6);
    memset(packet + 30, 255, 4);
    be16(packet + 34, 67);
    be16(packet + 36, 68);
    protected_packet(packet, length, "DHCPv4 broadcast");
    packet[30] = 224;
    protected_packet(packet, length, "DHCPv4 protected ports");
    length = ipv4(packet);
    packet[23] = 2;
    protected_packet(packet, length, "IGMP membership control");
    length = ipv4(packet);
    be16(packet + 12, 0x0806);
    protected_packet(packet, length, "ARP");
    be16(packet + 12, 0x888e);
    protected_packet(packet, length, "EAPOL");
    for (unsigned icmp = 130; icmp <= 137; ++icmp) {
        length = ipv6(packet);
        packet[20] = 58;
        packet[54] = (uint8_t)icmp;
        protected_packet(packet, length, "ICMPv6 RA NS NA RS MLD");
    }
    length = ipv6(packet);
    be16(packet + 54, 547);
    be16(packet + 56, 546);
    protected_packet(packet, length, "DHCPv6 client");
    be16(packet + 56, 547);
    protected_packet(packet, length, "DHCPv6 relay");
    for (unsigned protocol = 0; protocol <= 60; ++protocol) {
        if (protocol != 0 && protocol != 43 && protocol != 60 && protocol != 51)
            continue;
        length = ipv6(packet);
        memmove(packet + 62, packet + 54, 8);
        memset(packet + 54, 0, 8);
        packet[20] = (uint8_t)protocol;
        packet[54] = 17;
        be16(packet + 18, 16);
        length += 8;
        expect(rxfilter_packet(4, packet, length) == RXFILTER_DROP, "UDP after extension filtered");
        packet[54] = 58;
        protected_packet(packet, length, "ICMPv6 after extension");
    }
    length = ipv4(packet);
    be16(packet + 20, 0x2000);
    protected_packet(packet, length, "IPv4 fragment fail-open");
    length = ipv6(packet);
    packet[20] = 44;
    protected_packet(packet, length, "IPv6 fragment fail-open");
    packet[20] = 50;
    protected_packet(packet, length, "IPv6 ESP fail-open");
    length = ipv4(packet);
    for (unsigned tags = 0; tags < 3; ++tags) {
        memmove(packet + 16, packet + 12, length - 12);
        be16(packet + 12, 0x8100);
        be16(packet + 14, 0);
        length += 4;
        expect(rxfilter_packet(4, packet, length) == (tags < 2 ? RXFILTER_DROP : RXFILTER_INHERIT), "bounded VLAN parsing");
    }
    for (unsigned version = 0; version < 2; ++version) {
        length = version ? ipv6(packet) : ipv4(packet);
        for (size_t truncated = 0; truncated < length; ++truncated) {
            uint8_t* exact = malloc(truncated ? truncated : 1);
            memcpy(exact, packet, truncated);
            protected_packet(exact, truncated, "truncated packet fail-open");
            free(exact);
        }
    }
    uint32_t random = 0x46d8317u;
    for (size_t size = 0; size < 512; ++size) {
        uint8_t* exact = malloc(size ? size : 1);
        for (unsigned turn = 0; turn < 64; ++turn) {
            for (size_t i = 0; i < size; ++i) {
                random = random * 1664525u + 1013904223u;
                exact[i] = (uint8_t)(random >> 24);
            }
            enum rxfilter_verdict result = rxfilter_packet(4, exact, size);
            expect(result == RXFILTER_INHERIT || result == RXFILTER_DROP, "bounded packet mutation");
        }
        free(exact);
    }
    printf("%u checks, %u failures\n", cases, failures);
    return failures != 0;
}
