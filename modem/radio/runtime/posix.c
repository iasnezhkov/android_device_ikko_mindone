/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

struct mindone_radio_pollfd { int fd; uint32_t interests; uint32_t events; };

int mindone_radio_open(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}

void mindone_radio_close(int fd)
{
    close(fd);
}

ssize_t mindone_radio_read(int fd, uint8_t *bytes, size_t length)
{
    ssize_t count = read(fd, bytes, length);
    return count < 0 ? -errno : count;
}

ssize_t mindone_radio_write(int fd, const uint8_t *bytes, size_t length)
{
    ssize_t count = write(fd, bytes, length);
    return count < 0 ? -errno : count;
}

int mindone_radio_pipe(int *descriptors)
{
    if (pipe(descriptors)) return -errno;
    for (size_t i = 0; i < 2; ++i) {
        if (fcntl(descriptors[i], F_SETFL, O_NONBLOCK) < 0 ||
            fcntl(descriptors[i], F_SETFD, FD_CLOEXEC) < 0) {
            int error = errno;
            close(descriptors[0]);
            close(descriptors[1]);
            return -error;
        }
    }
    return 0;
}

int mindone_radio_poll(struct mindone_radio_pollfd *entries, size_t count, int timeout)
{
    struct pollfd descriptors[6];
    if (count > 6 || timeout < -1) return -EINVAL;
    for (size_t i = 0; i < count; ++i) {
        descriptors[i].fd = entries[i].fd;
        descriptors[i].events = ((entries[i].interests & 1) ? POLLIN : 0) |
                                ((entries[i].interests & 2) ? POLLOUT : 0);
        descriptors[i].revents = 0;
        entries[i].events = 0;
    }
    if (poll(descriptors, (nfds_t)count, timeout) < 0) return -errno;
    for (size_t i = 0; i < count; ++i) {
        short events = descriptors[i].revents;
        entries[i].events = ((events & POLLIN) ? 1U : 0U) |
                            ((events & POLLOUT) ? 2U : 0U) |
                            ((events & (POLLHUP | POLLERR | POLLNVAL)) ? 4U : 0U);
    }
    return 0;
}
