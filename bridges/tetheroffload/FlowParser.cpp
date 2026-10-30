/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "FlowParser.h"
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <cstring>
#include <map>
namespace mindone::bridges::tetheroffload {
namespace {
struct Value { const uint8_t* data; size_t size; };
using Attributes = std::map<unsigned, Value>;
bool attributes(const uint8_t* data, size_t size, Attributes* out) {
    while (size) {
        if (size < sizeof(nlattr)) return false;
        nlattr h;
        memcpy(&h, data, sizeof(h));
        if (h.nla_len < sizeof(h) || h.nla_len > size) return false;
        size_t aligned = NLA_ALIGN(h.nla_len);
        if (aligned > size) return false;
        if (!out->emplace(h.nla_type & NLA_TYPE_MASK,
            Value{data + sizeof(h), h.nla_len - sizeof(h)}).second) return false;
        data += aligned;
        size -= aligned;
    }
    return true;
}
bool nested(const Attributes& parent, unsigned type, Attributes* out) {
    auto it = parent.find(type);
    return it != parent.end() && attributes(it->second.data, it->second.size, out);
}
bool copy(const Attributes& a, unsigned type, void* dst, size_t size) {
    auto it = a.find(type);
    if (it == a.end() || it->second.size != size) return false;
    memcpy(dst, it->second.data, size);
    return true;
}
bool tuple(const Attributes& a, unsigned type, int family,
    std::array<uint8_t, 16>* src, std::array<uint8_t, 16>* dst,
    uint16_t* sport, uint16_t* dport, uint8_t* protocol) {
    Attributes tuple, ip, proto;
    bool v4 = family == AF_INET;
    return nested(a, type, &tuple) && nested(tuple, CTA_TUPLE_IP, &ip) &&
        nested(tuple, CTA_TUPLE_PROTO, &proto) &&
        copy(ip, v4 ? CTA_IP_V4_SRC : CTA_IP_V6_SRC, src->data(), v4 ? 4 : 16) &&
        copy(ip, v4 ? CTA_IP_V4_DST : CTA_IP_V6_DST, dst->data(), v4 ? 4 : 16) &&
        copy(proto, CTA_PROTO_NUM, protocol, 1) &&
        copy(proto, CTA_PROTO_SRC_PORT, sport, 2) && copy(proto, CTA_PROTO_DST_PORT, dport, 2);
}
}
std::string Flow::key() const {
    std::string value(1, static_cast<char>(family));
    value.push_back(static_cast<char>(proto));
    value.append(reinterpret_cast<const char*>(src.data()), src.size());
    value.append(reinterpret_cast<const char*>(dst.data()), dst.size());
    value.append(reinterpret_cast<const char*>(&sport), sizeof(sport));
    value.append(reinterpret_cast<const char*>(&dport), sizeof(dport));
    return value;
}
bool parseFlow(const void* buffer, size_t size, Flow* out) {
    if (size < NLMSG_LENGTH(sizeof(nfgenmsg))) return false;
    nlmsghdr h;
    memcpy(&h, buffer, sizeof(h));
    if (h.nlmsg_len != size || (h.nlmsg_type >> 8) != NFNL_SUBSYS_CTNETLINK) return false;
    unsigned command = h.nlmsg_type & 255;
    if (command != IPCTNL_MSG_CT_NEW && command != IPCTNL_MSG_CT_DELETE) return false;
    const auto* data = static_cast<const uint8_t*>(buffer);
    nfgenmsg gen;
    memcpy(&gen, data + NLMSG_HDRLEN, sizeof(gen));
    if ((gen.nfgen_family != AF_INET && gen.nfgen_family != AF_INET6) ||
        gen.version != NFNETLINK_V0) return false;
    Flow f;
    f.family = gen.nfgen_family;
    f.destroy = command == IPCTNL_MSG_CT_DELETE;
    Attributes a;
    size_t offset = NLMSG_LENGTH(sizeof(gen));
    if (!attributes(data + offset, size - offset, &a) ||
        !tuple(a, CTA_TUPLE_ORIG, f.family, &f.src, &f.dst, &f.sport, &f.dport, &f.proto) ||
        (f.proto != IPPROTO_TCP && f.proto != IPPROTO_UDP)) return false;
    if (a.count(CTA_ID) && !copy(a, CTA_ID, &f.id, sizeof(f.id))) return false;
    auto zone = a.find(CTA_ZONE);
    if (zone != a.end()) {
        uint16_t value = 0;
        if (!copy(a, CTA_ZONE, &value, sizeof(value)) || value != 0) return false;
    }
    uint8_t replyProto = 0;
    if (!f.destroy && (!tuple(a, CTA_TUPLE_REPLY, f.family, &f.replySrc, &f.pub,
        &f.replyPort, &f.publicPort, &replyProto) || replyProto != f.proto ||
        !copy(a, CTA_TIMEOUT, &f.timeout, 4) || !copy(a, CTA_STATUS, &f.status, 4))) return false;
    f.timeout = ntohl(f.timeout);
    f.status = ntohl(f.status);
    *out = f;
    return true;
}
bool contains(const std::string& prefix, int family, const std::array<uint8_t, 16>& ip) {
    auto slash = prefix.find('/');
    if (slash == std::string::npos) return false;
    std::array<uint8_t, 16> network{};
    if (inet_pton(family, prefix.substr(0, slash).c_str(), network.data()) != 1) return false;
    unsigned bits = 0;
    for (char c : prefix.substr(slash + 1)) {
        if (c < '0' || c > '9' || bits > 128) return false;
        bits = bits * 10 + c - '0';
    }
    if (bits > (family == AF_INET ? 32u : 128u)) return false;
    for (unsigned i = 0; i < bits; ++i)
        if ((ip[i / 8] ^ network[i / 8]) & (128 >> (i % 8))) return false;
    return true;
}
}
