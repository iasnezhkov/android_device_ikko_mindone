/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vsim-adaptor"

#include <android/log.h>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

namespace {

int g_sig_fd = -1;

void onSignal(int) {
    if (g_sig_fd >= 0) {
        uint8_t byte = 1;
        ssize_t ignored = write(g_sig_fd, &byte, 1);
        (void)ignored;
    }
}

}

extern "C" {

int vsim_net_listen_abstract(const char* name) {
    if (!name) return -1;
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len + 1 >= sizeof(sockaddr_un::sun_path)) return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(addr.sun_path + 1, name, name_len);
    socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name_len);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), len) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "bind @%s failed: %s", name,
                            strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "listen @%s failed: %s", name,
                            strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int vsim_net_accept(int listen_fd, uint32_t* pid, uint32_t* uid, uint32_t* gid, char* secctx,
                    size_t secctx_cap) {
    int fd = accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -2 : -1;

    ucred cred{};
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0) {
        close(fd);
        return -1;
    }
    if (pid) *pid = static_cast<uint32_t>(cred.pid);
    if (uid) *uid = static_cast<uint32_t>(cred.uid);
    if (gid) *gid = static_cast<uint32_t>(cred.gid);

    if (secctx && secctx_cap > 0) {
        socklen_t ctx_len = static_cast<socklen_t>(secctx_cap - 1);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, secctx, &ctx_len) == 0) {
            if (ctx_len >= secctx_cap) ctx_len = static_cast<socklen_t>(secctx_cap - 1);
            secctx[ctx_len] = '\0';
        } else {
            secctx[0] = '\0';
        }
    }
    return fd;
}

int vsim_net_read(int fd, uint8_t* buf, size_t cap) {
    if (fd < 0 || !buf || cap == 0) return -1;
    ssize_t n = read(fd, buf, cap);
    if (n > 0) return static_cast<int>(n);
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return -2;
    return -1;
}

int vsim_net_write_all(int fd, const uint8_t* buf, size_t len) {
    if (fd < 0 || (!buf && len > 0)) return -1;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int rc = poll(&pfd, 1, 2000);
            if (rc > 0 && (pfd.revents & POLLOUT)) continue;
        }
        return -1;
    }
    return 0;
}

void vsim_net_close(int fd) {
    if (fd >= 0) close(fd);
}

int vsim_net_pipe(int fds[2]) { return pipe2(fds, O_NONBLOCK | O_CLOEXEC); }

void vsim_net_wake(int write_fd) {
    if (write_fd >= 0) {
        uint8_t byte = 1;
        ssize_t ignored = write(write_fd, &byte, 1);
        (void)ignored;
    }
}

void vsim_net_drain(int read_fd) {
    if (read_fd < 0) return;
    uint8_t buf[64];
    while (read(read_fd, buf, sizeof(buf)) > 0) {
    }
}

int vsim_net_poll(int listen_fd, int client_fd, int wake_fd, int sig_fd, int timeout_ms,
                  uint32_t* mask) {
    pollfd pfds[4]{};
    pfds[0].fd = listen_fd;
    pfds[0].events = listen_fd >= 0 ? POLLIN : 0;
    pfds[1].fd = client_fd;
    pfds[1].events = client_fd >= 0 ? POLLIN : 0;
    pfds[2].fd = wake_fd;
    pfds[2].events = wake_fd >= 0 ? POLLIN : 0;
    pfds[3].fd = sig_fd;
    pfds[3].events = sig_fd >= 0 ? POLLIN : 0;

    int rc = poll(pfds, 4, timeout_ms);
    if (rc < 0) {
        if (errno == EINTR) {
            if (mask) *mask = 0;
            return 0;
        }
        return -1;
    }
    uint32_t out = 0;
    if (pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) out |= 1u << 0;
    if (pfds[1].revents & (POLLIN | POLLERR | POLLHUP)) out |= 1u << 1;
    if (pfds[2].revents & (POLLIN | POLLERR | POLLHUP)) out |= 1u << 2;
    if (pfds[3].revents & (POLLIN | POLLERR | POLLHUP)) out |= 1u << 3;
    if (mask) *mask = out;
    return rc;
}

uint64_t vsim_net_now_ms(void) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

int vsim_net_install_signals(int sig_write_fd) {
    signal(SIGPIPE, SIG_IGN);
    g_sig_fd = sig_write_fd;
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, nullptr) != 0) return -1;
    if (sigaction(SIGINT, &sa, nullptr) != 0) return -1;
    return 0;
}

int vsim_prop_get(const char* name, char* out, size_t cap) {
    if (!name || !out || cap < PROP_VALUE_MAX) return -1;
    out[0] = '\0';
    return __system_property_get(name, out);
}

void vsim_log_msg(int level, const char* msg) {
    if (!msg) return;
    int prio = ANDROID_LOG_INFO;
    if (level == 1) prio = ANDROID_LOG_WARN;
    else if (level >= 2) prio = ANDROID_LOG_ERROR;
    __android_log_write(prio, LOG_TAG, msg);
}

}
