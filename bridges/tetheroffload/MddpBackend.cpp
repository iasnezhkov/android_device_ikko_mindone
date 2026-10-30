/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "MddpBackend"
#include "MddpBackend.h"
#include <aidl/android/hardware/tetheroffload/NatTimeoutUpdate.h>
#include <android-base/logging.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <cstddef>
#include <cerrno>
#include <cstdlib>
namespace mindone::bridges::tetheroffload {
namespace {
struct Header { uint16_t magic, version; uint32_t app, msg, length; };
struct Packet { Header h; std::array<uint8_t, 256> data; };
static_assert(sizeof(Header) == 16 && offsetof(Packet, data) == 16);
using Status = ndk::ScopedAStatus;
Status bad() { return Status::fromExceptionCode(EX_ILLEGAL_ARGUMENT); }
Status state() { return Status::fromExceptionCode(EX_ILLEGAL_STATE); }
Status result(bool ok) {
    return ok ? Status::ok() : Status::fromServiceSpecificErrorWithMessage(
        aidl_to::IOffload::ERROR_CODE_UNUSED, "MDDP or rtnetlink operation failed");
}
bool name(const std::string& s) {
    return !s.empty() && s.size() < IFNAMSIZ && s.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.:") == std::string::npos;
}
bool address(const std::string& s, int family) {
    in6_addr value{};
    return s.find('\0') == std::string::npos && inet_pton(family, s.c_str(), &value) == 1;
}
bool prefix(const std::string& s) {
    auto slash = s.find('/');
    if (slash == std::string::npos || slash + 1 == s.size()) return false;
    auto ip = s.substr(0, slash), mask = s.substr(slash + 1);
    if (mask.size() > 3 || mask.find_first_not_of("0123456789") != std::string::npos) return false;
    unsigned bits = 0;
    for (char c : mask) bits = bits * 10 + c - '0';
    return (address(ip, AF_INET) && bits <= 32) || (address(ip, AF_INET6) && bits <= 128);
}
uint32_t sysctl(const char* path, uint32_t fallback) {
    std::ifstream stream(path);
    uint64_t value = 0;
    return stream >> value && value > 0 && value <= UINT32_MAX ? value : fallback;
}
template<class T> T load(const uint8_t* p) { T value; memcpy(&value, p, sizeof(value)); return value; }
struct RouteMessage {
    nlmsghdr header{};
    std::array<uint8_t, 2048> payload{};
    template<class T> RouteMessage(uint16_t type, const T& value) {
        header.nlmsg_type = type;
        header.nlmsg_len = NLMSG_LENGTH(sizeof(value));
        memcpy(payload.data(), &value, sizeof(value));
    }
    bool attribute(uint16_t type, const void* data, size_t size) {
        size_t offset = NLMSG_ALIGN(header.nlmsg_len);
        size_t length = RTA_LENGTH(size);
        if (offset + RTA_ALIGN(length) > sizeof(*this)) return false;
        auto* attr = reinterpret_cast<rtattr*>(reinterpret_cast<uint8_t*>(this) + offset);
        attr->rta_type = type;
        attr->rta_len = static_cast<uint16_t>(length);
        memcpy(RTA_DATA(attr), data, size);
        header.nlmsg_len = offset + RTA_ALIGN(length);
        return true;
    }
};
static_assert(offsetof(RouteMessage, payload) == NLMSG_HDRLEN);
using RouteReplies = std::vector<std::vector<uint8_t>>;
class RouteSocket {
  public:
    RouteSocket() : mFd(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)) {
        sockaddr_nl local{};
        local.nl_family = AF_NETLINK;
        timeval timeout{2, 0};
        if (mFd.get() >= 0 && (bind(mFd.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0 ||
            setsockopt(mFd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)) mFd.reset();
    }
    bool transact(nlmsghdr* request, RouteReplies* replies = nullptr, int absent = 0) {
        if (mFd.get() < 0) return false;
        request->nlmsg_seq = ++mSequence;
        request->nlmsg_flags = NLM_F_REQUEST | (replies ? NLM_F_DUMP : NLM_F_ACK);
        sockaddr_nl kernel{};
        kernel.nl_family = AF_NETLINK;
        ssize_t sent;
        do {
            sent = sendto(mFd.get(), request, request->nlmsg_len, 0,
                reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel));
        } while (sent < 0 && errno == EINTR);
        if (sent != static_cast<ssize_t>(request->nlmsg_len)) return false;
        for (;;) {
            alignas(nlmsghdr) std::array<uint8_t, 32768> buffer{};
            sockaddr_nl sender{};
            iovec iov{buffer.data(), buffer.size()};
            msghdr message{};
            message.msg_name = &sender;
            message.msg_namelen = sizeof(sender);
            message.msg_iov = &iov;
            message.msg_iovlen = 1;
            ssize_t size;
            do { size = recvmsg(mFd.get(), &message, 0); } while (size < 0 && errno == EINTR);
            if (size <= 0 || (message.msg_flags & MSG_TRUNC)) return false;
            if (message.msg_namelen != sizeof(sender) || sender.nl_pid != 0) return false;
            int remaining = static_cast<int>(size);
            for (auto* h = reinterpret_cast<nlmsghdr*>(buffer.data()); NLMSG_OK(h, remaining);
                 h = NLMSG_NEXT(h, remaining)) {
                if (h->nlmsg_seq != mSequence) continue;
                if (h->nlmsg_flags & NLM_F_DUMP_INTR) return false;
                if (h->nlmsg_type == NLMSG_ERROR) {
                    if (h->nlmsg_len < NLMSG_LENGTH(sizeof(nlmsgerr))) return false;
                    int error = reinterpret_cast<nlmsgerr*>(NLMSG_DATA(h))->error;
                    if (error != 0 && error != -absent) {
                        LOG(ERROR) << "rtnetlink type=" << request->nlmsg_type << ": " << strerror(-error);
                        return false;
                    }
                    if (!replies) return true;
                } else if (h->nlmsg_type == NLMSG_DONE) {
                    if (!replies) return false;
                    if (h->nlmsg_len >= NLMSG_LENGTH(sizeof(int)) &&
                        load<int>(static_cast<uint8_t*>(NLMSG_DATA(h))) != 0) return false;
                    return true;
                } else if (h->nlmsg_type == NLMSG_OVERRUN) {
                    return false;
                } else if (replies) {
                    auto* first = reinterpret_cast<uint8_t*>(h);
                    replies->emplace_back(first, first + h->nlmsg_len);
                }
            }
            if (remaining != 0) return false;
        }
    }
  private:
    ::android::base::unique_fd mFd;
    uint32_t mSequence = 0;
};
bool connectionMode() {
    std::ifstream stream("/sys/class/misc/mddp/mddp_method");
    std::string line;
    return std::getline(stream, line) && line == "MDDP is Connection base now";
}
bool neighbor(const Flow& flow, const std::string& iface, std::array<uint8_t, 6>* mac) {
    RouteSocket route;
    ndmsg info{};
    info.ndm_family = flow.family;
    RouteMessage request(RTM_GETNEIGH, info);
    RouteReplies replies;
    if (!route.transact(&request.header, &replies)) return false;
    unsigned index = if_nametoindex(iface.c_str());
    for (const auto& reply : replies) {
        auto* h = reinterpret_cast<const nlmsghdr*>(reply.data());
        if (h->nlmsg_type != RTM_NEWNEIGH || h->nlmsg_len < NLMSG_LENGTH(sizeof(ndmsg))) continue;
        auto* entry = static_cast<const ndmsg*>(NLMSG_DATA(h));
        if (entry->ndm_family != flow.family || static_cast<unsigned>(entry->ndm_ifindex) != index ||
            !(entry->ndm_state & (NUD_REACHABLE | NUD_STALE | NUD_DELAY | NUD_PROBE | NUD_PERMANENT))) continue;
        int length = h->nlmsg_len - NLMSG_LENGTH(sizeof(ndmsg));
        bool found = false, hardware = false;
        for (auto* attr = reinterpret_cast<const rtattr*>(reinterpret_cast<const uint8_t*>(entry) +
             NLMSG_ALIGN(sizeof(ndmsg))); RTA_OK(attr, length); attr = RTA_NEXT(attr, length)) {
            if (attr->rta_type == NDA_DST && RTA_PAYLOAD(attr) == (flow.family == AF_INET ? 4u : 16u))
                found = memcmp(RTA_DATA(attr), flow.src.data(), RTA_PAYLOAD(attr)) == 0;
            if (attr->rta_type == NDA_LLADDR && RTA_PAYLOAD(attr) == 6) {
                memcpy(mac->data(), RTA_DATA(attr), 6);
                hardware = true;
            }
        }
        if (!length && found && hardware && !((*mac)[0] & 1) &&
            *mac != std::array<uint8_t, 6>{}) return true;
    }
    return false;
}
Status unsupported(const char* reason) {
    return Status::fromExceptionCodeWithMessage(EX_UNSUPPORTED_OPERATION, reason);
}
}
MddpBackend::MddpBackend()
    : mDevice(open("/dev/mddp", O_RDWR | O_CLOEXEC | O_NONBLOCK)),
      mWake(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    if (connect()) mEvents = std::thread(&MddpBackend::events, this);
}
MddpBackend::~MddpBackend() {
    mQuit = true;
    uint64_t wake = 1;
    if (mWake.get() >= 0) write(mWake.get(), &wake, sizeof(wake));
    if (mEvents.joinable()) mEvents.join();
    if (mInitialized) request(0xff, 1, nullptr, 0);
}
bool MddpBackend::connect() const { return mDevice.get() >= 0 && mWake.get() >= 0 && !mReaderFailed; }
void MddpBackend::wake() {
    uint64_t value = 1;
    ssize_t result;
    do { result = write(mWake.get(), &value, sizeof(value)); } while (result < 0 && errno == EINTR);
}
void MddpBackend::poison() {
    mReaderFailed = true;
    wake();
}
void MddpBackend::notice(int event, std::vector<uint8_t> data) {
    if (mNotices.size() >= 1024) {
        poison();
        return;
    }
    mNotices.push_back({mGeneration, event, std::move(data)});
    wake();
}
bool MddpBackend::request(uint32_t app, uint32_t msg, void* data, uint32_t length) {
    Packet p{{0x2454, 2, app, msg, length}, {}};
    if (length > p.data.size()) return false;
    if (length) memcpy(p.data.data(), data, length);
    int rc;
    do { rc = ioctl(mDevice.get(), 0, &p); } while (rc < 0 && errno == EINTR);
    if (rc != 0) { PLOG(ERROR) << "MDDP app=" << app << " msg=" << msg; return false; }
    if (msg == 4) {
        if (p.h.magic != 0x2454 || p.h.version != 0 || p.h.app != app ||
            p.h.msg != msg || p.h.length != 128) return false;
        memcpy(data, p.data.data(), 128);
    }
    return true;
}
bool MddpBackend::receive() {
    for (size_t count = 0; count < 256; ++count) {
        Packet p{};
        ssize_t size = read(mDevice.get(), &p, sizeof(p));
        if (size < 0 && errno == EINTR) continue;
        if (size < 0 && errno == EAGAIN) return true;
        if (size < 16 || p.h.magic != 0x2454 || p.h.length > p.data.size() ||
            size != static_cast<ssize_t>(16 + p.h.length)) {
            poison();
            return false;
        }
        notification(p.h.app, p.h.msg, p.h.version, p.data.data(), p.h.length);
        if (mReaderFailed) return false;
    }
    poison();
    return false;
}

bool MddpBackend::control(uint32_t command, uint16_t expected) {
    if (!receive()) return false;
    if (command == 2 && (mUpstream.empty() || mDownstream.empty() || (!mV4 && !mV6))) return false;
    char names[32]{};
    memcpy(names, mUpstream.data(), mUpstream.size());
    memcpy(names + 16, mDownstream.data(), mDownstream.size());
    if (!request(command < 2 ? 255 : 1, command, command == 2 ? names : nullptr,
        command == 2 ? sizeof(names) : 0)) return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        Packet p{};
        ssize_t size = read(mDevice.get(), &p, sizeof(p));
        if (size < 0 && (errno == EAGAIN || errno == EINTR)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (size < 16 || p.h.magic != 0x2454 || p.h.length > p.data.size() ||
            size != static_cast<ssize_t>(16 + p.h.length)) break;
        if (p.h.app == 1 && p.h.msg == 0x100 + command && p.h.length == 4) {
            if (p.h.version != expected) return false;
            if (command == 0) notice(4);
            return true;
        }
        notification(p.h.app, p.h.msg, p.h.version, p.data.data(), p.h.length);
    }
    poison();
    request(255, 1, nullptr, 0);
    return false;
}
bool MddpBackend::drain() {
    if (mUpstream.empty()) return true;
    std::array<uint8_t, 128> data{};
    if (!request(255, 4, data.data(), 0)) return false;
    auto& stats = mStats[mUpstream];
    auto add = [](int64_t current, uint64_t delta) {
        return delta > static_cast<uint64_t>(INT64_MAX - current) ? INT64_MAX :
            current + static_cast<int64_t>(delta);
    };
    stats.txBytes = add(stats.txBytes, load<uint64_t>(data.data()));
    stats.rxBytes = add(stats.rxBytes, load<uint64_t>(data.data() + 64));
    return true;
}
bool MddpBackend::inject(const Flow& f, bool destroy, const std::string& downstream) {
    std::array<uint8_t, 52> data{};
    bool v4 = f.family == AF_INET;
    size_t addressSize = v4 ? 4 : 16;
    memcpy(data.data(), f.src.data(), addressSize);
    memcpy(data.data() + addressSize, f.dst.data(), addressSize);
    if (v4) memcpy(data.data() + 8, f.pub.data(), 4);
    size_t port = v4 ? 12 : 32;
    memcpy(data.data() + port, &f.sport, 2);
    memcpy(data.data() + port + 2, &f.dport, 2);
    if (v4) memcpy(data.data() + 16, &f.publicPort, 2);
    data[v4 ? 18 : 36] = f.proto;
    std::array<uint8_t, 6> mac = f.mac;
    if (!destroy && !neighbor(f, downstream, &mac)) return true;
    memcpy(data.data() + (v4 ? 19 : 37), mac.data(), 6);
    memcpy(data.data() + (v4 ? 32 : 48), &f.timeout, 4);
    if (!request(255, (v4 ? 8 : 10) + (destroy ? 1 : 0), data.data(), v4 ? 36 : 52)) return false;
    if (!destroy) {
        Flow saved = f;
        saved.mac = mac;
        mFlows[f.key()] = saved;
    }
    return true;
}
bool MddpBackend::flushFlows() {
    for (const auto& [key, flow] : mFlows) {
        (void)key;
        if (!inject(flow, true, mDownstream)) return false;
    }
    mFlows.clear();
    return true;
}
bool MddpBackend::deactivate() {
    if (!mActive) return true;
    mActive = false;
    mQuota = false;
    if (!drain() || !flushFlows() || !control(3, 3) || !drain()) {
        poison();
        request(255, 1, nullptr, 0);
        return false;
    }
    mDownstream.clear();
    return true;
}
bool MddpBackend::reconcile() {
    if (mReaderFailed) return false;
    std::string desired;
    if (!mUpstream.empty() && (mV4 || mV6))
        for (const auto& entry : mDownstreams) {
            if (!desired.empty() && desired != entry.first) return false;
            desired = entry.first;
        }
    if (mActive && desired != mDownstream && !deactivate()) return false;
    if (desired.empty() || mActive) return true;
    if (!connectionMode()) return false;
    mDownstream = desired;
    if (!control(2, 1)) return false;
    mActive = true;
    if (!connectionMode()) { deactivate(); return false; }
    uint32_t values[2] = {
        sysctl("/proc/sys/net/netfilter/nf_conntrack_udp_timeout_stream", 180),
        sysctl("/proc/sys/net/netfilter/nf_conntrack_tcp_timeout_established", 432000)};
    if (!request(255, 6, values, sizeof(values))) { deactivate(); return false; }
    notice(1);
    return true;
}
bool MddpBackend::admit(const Flow& f, std::string* downstream) const {
    if (!mActive || (f.family == AF_INET ? !mV4 : !mV6) || !f.timeout ||
        !(f.status & (1u << 1)) || (f.proto == IPPROTO_TCP && !(f.status & (1u << 2))) ||
        f.replySrc != f.dst || f.replyPort != f.dport) return false;
    if (f.family == AF_INET) {
        std::array<uint8_t, 16> expected{};
        if (inet_pton(AF_INET, mV4Address.c_str(), expected.data()) != 1 || expected != f.pub) return false;
    } else if (f.src != f.pub || f.sport != f.publicPort) return false;
    for (const auto& p : mPrefixes)
        if (contains(p, f.family, f.src) || contains(p, f.family, f.dst)) return false;
    for (const auto& entry : mDownstreams)
        if (entry.first == mDownstream && contains(entry.second, f.family, f.src)) {
            *downstream = entry.first;
            return true;
        }
    return false;
}
bool MddpBackend::conntrack(int fd) {
    alignas(nlmsghdr) std::array<uint8_t, 65536> data{};
    sockaddr_nl sender{};
    iovec iov{data.data(), data.size()};
    msghdr message{};
    message.msg_name = &sender;
    message.msg_namelen = sizeof(sender);
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    ssize_t size = recvmsg(fd, &message, MSG_DONTWAIT);
    if (size < 0) return errno == EAGAIN || errno == EINTR;
    if (!size || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
        message.msg_namelen != sizeof(sender) || sender.nl_pid != 0) return false;
    int remaining = size;
    for (auto* h = reinterpret_cast<nlmsghdr*>(data.data()); NLMSG_OK(h, remaining);
         h = NLMSG_NEXT(h, remaining)) {
        if (h->nlmsg_type == NLMSG_OVERRUN || h->nlmsg_type == NLMSG_ERROR) return false;
        Flow flow;
        if (!parseFlow(h, h->nlmsg_len, &flow)) continue;
        auto it = mFlows.find(flow.key());
        if (flow.destroy && it != mFlows.end() && flow.id && it->second.id &&
            flow.id != it->second.id) continue;
        std::string downstream;
        bool allowed = !flow.destroy && admit(flow, &downstream);
        if (it != mFlows.end()) {
            if (!inject(it->second, true, mDownstream)) return false;
            mFlows.erase(it);
        }
        if (allowed && mFlows.size() >= 8192) continue;
        if (allowed && !inject(flow, false, downstream)) return false;
    }
    return remaining == 0;
}
Status MddpBackend::initOffload(const ndk::ScopedFileDescriptor& fd1,
    const ndk::ScopedFileDescriptor& fd2, const std::shared_ptr<aidl_to::ITetheringOffloadCallback>& cb) {
    std::lock_guard delivery(mDelivery);
    std::lock_guard lock(mLock);
    if (mInitialized) return state();
    if (!cb || fd1.get() < 0 || fd2.get() < 0) return bad();
    for (int fd : {fd1.get(), fd2.get()}) {
        sockaddr_nl local{};
        socklen_t length = sizeof(local);
        int protocol = 0;
        socklen_t protocolLength = sizeof(protocol);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length) ||
            length != sizeof(local) || local.nl_family != AF_NETLINK ||
            getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &protocolLength) ||
            protocol != NETLINK_NETFILTER) return bad();
    }
    ::android::base::unique_fd one(fcntl(fd1.get(), F_DUPFD_CLOEXEC, 0));
    ::android::base::unique_fd two(fcntl(fd2.get(), F_DUPFD_CLOEXEC, 0));
    if (one.get() < 0 || two.get() < 0 || !connect()) return result(false);
    ++mGeneration;
    mNotices.clear();
    if (!control(0, 4)) return result(false);
    if (!connectionMode()) {
        request(255, 1, nullptr, 0);
        return unsupported("UNSUPPORTED: Packet mode cannot enforce prefix policy");
    }
    mCallback = cb;
    mConntrack1 = std::move(one);
    mConntrack2 = std::move(two);
    mInitialized = true;
    uint64_t wake = 1;
    write(mWake.get(), &wake, sizeof(wake));
    return Status::ok();
}
Status MddpBackend::stopOffload() {
    std::lock_guard delivery(mDelivery);
    std::lock_guard lock(mLock);
    if (!mInitialized) return state();
    if (!mReaderFailed && !deactivate()) return result(false);
    if (!request(255, 1, nullptr, 0)) return result(false);
    ++mGeneration;
    mInitialized = false;
    mCallback.reset();
    mConntrack1.reset();
    mConntrack2.reset();
    mUpstream.clear();
    mDownstreams.clear();
    mPrefixes.clear();
    mStats.clear();
    mNotices.clear();
    mFlows.clear();
    mV4 = mV6 = mQuota = false;
    wake();
    return Status::ok();
}
Status MddpBackend::setLocalPrefixes(const std::vector<std::string>& prefixes) {
    std::lock_guard lock(mLock);
    if (!mInitialized) return state();
    if (mReaderFailed) return result(false);
    if (prefixes.empty()) return bad();
    for (const auto& p : prefixes) if (!prefix(p)) return bad();
    if (!flushFlows()) return result(false);
    mPrefixes = prefixes;
    return Status::ok();
}
Status MddpBackend::getForwardedStats(const std::string& upstream, aidl_to::ForwardedStats* out) {
    std::lock_guard lock(mLock);
    out->rxBytes = out->txBytes = 0;
    if (!name(upstream)) return bad();
    if (mInitialized && upstream == mUpstream && mActive && !drain()) return result(false);
    auto it = mStats.find(upstream);
    if (it != mStats.end()) {
        *out = it->second;
        mStats.erase(it);
    }
    return Status::ok();
}
Status MddpBackend::setDataWarningAndLimit(const std::string& upstream, int64_t warning, int64_t limit) {
    std::lock_guard delivery(mDelivery);
    std::lock_guard lock(mLock);
    if (!mInitialized) return state();
    if (mReaderFailed) return result(false);
    if (!name(upstream) || warning < 0 || limit < 0 || warning > limit) return bad();
    if (!receive() || upstream != mUpstream || !mActive) return result(false);
    struct Quota { char iface[16]; uint64_t limit, warning; } value{};
    static_assert(sizeof(Quota) == 32);
    memcpy(value.iface, upstream.data(), upstream.size());
    value.limit = limit;
    value.warning = warning;
    if (!request(255, 7, &value, sizeof(value))) return result(false);
    mNotices.erase(std::remove_if(mNotices.begin(), mNotices.end(),
        [](const Notice& notice) { return notice.event == 6; }), mNotices.end());
    mQuota = true;
    mWarned = false;
    return Status::ok();
}
Status MddpBackend::setUpstreamParameters(const std::string& iface, const std::string& v4Addr,
    const std::string& v4Gw, const std::vector<std::string>& v6Gws) {
    std::lock_guard lock(mLock);
    if (!mInitialized) return state();
    if (mReaderFailed) return result(false);
    if ((!iface.empty() && !name(iface)) || (!v4Addr.empty() && !address(v4Addr, AF_INET)) ||
        (!v4Gw.empty() && !address(v4Gw, AF_INET))) return bad();
    for (const auto& gw : v6Gws) if (!address(gw, AF_INET6)) return bad();
    if (!iface.empty() && (iface.rfind("ccmni", 0) != 0 || iface.size() == 5 ||
        iface.substr(5).find_first_not_of("0123456789") != std::string::npos))
        return unsupported("UNSUPPORTED: upstream must be a modem CCMNI interface");
    if (!receive() || !deactivate()) return result(false);
    mUpstream = iface;
    mV4Address = v4Addr;
    mV4 = !iface.empty() && !v4Addr.empty() && !v4Gw.empty();
    mV6 = !iface.empty() && !v6Gws.empty();
    return result(reconcile());
}
Status MddpBackend::addDownstream(const std::string& iface, const std::string& p) {
    std::lock_guard lock(mLock);
    if (!mInitialized) return state();
    if (mReaderFailed) return result(false);
    if (!name(iface) || !prefix(p)) return bad();
    if (iface.rfind("ap", 0) != 0)
        return unsupported("UNSUPPORTED: USB has no MDDP backend; only Wi-Fi AP is supported");
    for (const auto& entry : mDownstreams)
        if (entry.first != iface) return unsupported("UNSUPPORTED: only one Wi-Fi downstream is supported");
    auto entry = std::make_pair(iface, p);
    bool inserted = mDownstreams.insert(entry).second;
    if (reconcile()) return Status::ok();
    if (inserted) mDownstreams.erase(entry);
    return result(false);
}
Status MddpBackend::removeDownstream(const std::string& iface, const std::string& p) {
    std::lock_guard lock(mLock);
    if (!mInitialized) return state();
    if (mReaderFailed) return result(false);
    if (!name(iface) || !prefix(p)) return bad();
    auto entry = std::make_pair(iface, p);
    if (!mDownstreams.count(entry)) return Status::ok();
    if (!flushFlows()) return result(false);
    mDownstreams.erase(entry);
    return result(reconcile());
}
void MddpBackend::notification(uint32_t app, uint32_t msg, uint16_t status,
    const uint8_t* data, size_t size) {
    if (!mInitialized || (app != 1 && app != 255)) return;
    if (msg == 0x105 && status == 6 && size == 28) {
        if (data[18] == IPPROTO_TCP || data[18] == IPPROTO_UDP)
            notice(0, std::vector<uint8_t>(data, data + size));
        return;
    }
    if (msg == 0x100 && status == 4 && app == 1 && size == 4) {
        ++mGeneration;
        mNotices.clear();
        mActive = mQuota = mWarned = mV4 = mV6 = false;
        mFlows.clear();
        mUpstream.clear();
        mV4Address.clear();
        mDownstream.clear();
        mDownstreams.clear();
        mPrefixes.clear();
        notice(4);
        return;
    }
    if ((msg == 0x104 || msg == 0x106) && (app != 255 || size != 0)) return;
    if (msg == 0x106 && status == 7 && mActive && mQuota && !mWarned) {
        mWarned = true;
        notice(6);
        return;
    }
    bool limit = msg == 0x104 && status == 5 && mQuota && mActive;
    bool loss = (msg == 0x103 && status == 3) ||
        (msg >= 0x100 && msg <= 0x103 && status == 2);
    if (!limit && !loss) return;
    bool stopped = true;
    if (limit || (loss && mActive && msg != 0x103)) {
        mQuota = false;
        stopped = deactivate();
    } else {
        stopped = drain();
        mActive = false;
        mFlows.clear();
    }
    if (!stopped) {
        request(255, 1, nullptr, 0);
        poison();
    }
    mQuota = false;
    mDownstream.clear();
    mDownstreams.clear();
    mV4 = mV6 = false;
    notice(stopped ? (limit ? 5 : (status == 3 ? 3 : 2)) : 2);
}
void MddpBackend::events() {
    while (!mQuit) {
        {
            std::lock_guard lock(mLock);
            if (mInitialized && !mReaderFailed) {
                bool ok = receive();
                if (ok && mActive && !connectionMode()) ok = false;
                if (ok) ok = conntrack(mConntrack1.get()) && conntrack(mConntrack2.get());
                if (!ok) {
                    bool stopped = deactivate();
                    bool disabled = request(255, 1, nullptr, 0);
                    (void)stopped;
                    (void)disabled;
                    poison();
                    notice(2);
                }
            }
        }
        for (;;) {
            std::lock_guard delivery(mDelivery);
            Notice notice;
            std::shared_ptr<aidl_to::ITetheringOffloadCallback> cb;
            {
                std::lock_guard lock(mLock);
                if (mNotices.empty()) break;
                notice = std::move(mNotices.front());
                mNotices.pop_front();
                if (notice.generation != mGeneration) continue;
                cb = mCallback;
            }
            if (!cb) continue;
            if (notice.event) {
                auto status = cb->onEvent(static_cast<aidl_to::OffloadCallbackEvent>(notice.event));
                if (!status.isOk()) LOG(ERROR) << status.getDescription();
            } else {
                const auto* d = notice.data.data();
                bool dstNat = d[24] != 0;
                char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, d + (dstNat ? 4 : 0), src, sizeof(src));
                inet_ntop(AF_INET, d + (dstNat ? 8 : 4), dst, sizeof(dst));
                aidl_to::NatTimeoutUpdate update;
                update.src.addr = src;
                update.dst.addr = dst;
                update.src.port = ntohs(load<uint16_t>(d + (dstNat ? 14 : 12)));
                update.dst.port = ntohs(load<uint16_t>(d + (dstNat ? 16 : 14)));
                update.proto = static_cast<aidl_to::NetworkProtocol>(d[18]);
                auto status = cb->updateTimeout(update);
                if (!status.isOk()) LOG(ERROR) << status.getDescription();
            }
        }
        if (mReaderFailed) {
            LOG(ERROR) << "MDDP session failed; restarting service";
            _exit(EXIT_FAILURE);
        }
        std::array<pollfd, 4> descriptors{{{mWake.get(), POLLIN, 0}, {-1, POLLIN, 0},
                                        {-1, POLLIN, 0}, {-1, POLLIN, 0}}};
        ::android::base::unique_fd one, two;
        uint64_t generation;
        {
            std::lock_guard lock(mLock);
            generation = mGeneration;
            if (mInitialized) {
                one.reset(fcntl(mConntrack1.get(), F_DUPFD_CLOEXEC, 0));
                two.reset(fcntl(mConntrack2.get(), F_DUPFD_CLOEXEC, 0));
                if (one.get() < 0 || two.get() < 0) {
                    poison();
                    continue;
                }
                descriptors[1].fd = mDevice.get();
                descriptors[2].fd = one.get();
                descriptors[3].fd = two.get();
            }
        }
        int rc = poll(descriptors.data(), descriptors.size(), -1);
        if (rc < 0 && errno == EINTR) continue;
        if (descriptors[0].revents & POLLIN) {
            uint64_t value;
            read(mWake.get(), &value, sizeof(value));
        }
        {
            std::lock_guard lock(mLock);
            if (generation != mGeneration) continue;
            if (rc < 0 || std::any_of(descriptors.begin(), descriptors.end(),
                [](const pollfd& fd) { return fd.revents & (POLLERR | POLLHUP | POLLNVAL); })) {
                request(255, 1, nullptr, 0);
                poison();
            }
        }
    }
}
}
