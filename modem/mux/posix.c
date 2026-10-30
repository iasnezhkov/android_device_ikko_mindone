/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

struct mindone_mux_pollfd {
    int32_t fd;
    uint32_t interests;
    uint32_t events;
};

struct mindone_mux_link_id {
    uint64_t device;
    uint64_t inode;
};

int mindone_mux_errno_kind(int error)
{
    if (error == EINTR) return 1;
    if (error == EAGAIN || error == EWOULDBLOCK) return 2;
    if (error == EBUSY) return 3;
    if (error == ENODEV || error == ETXTBSY) return 4;
    return 5;
}

static int fail_fd(int fd, int error)
{
    close(fd);
    return -error;
}

int mindone_mux_check_fd(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -errno;
    if (!(flags & O_NONBLOCK)) return -EINVAL;
    flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -errno;
    return flags & FD_CLOEXEC ? 0 : -EINVAL;
}

int mindone_mux_open_serial(const char *path)
{
    struct stat status;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -errno;
    if (fstat(fd, &status)) return fail_fd(fd, errno);
    if (!S_ISCHR(status.st_mode)) return fail_fd(fd, ENOTTY);
    if (flock(fd, LOCK_EX | LOCK_NB)) return fail_fd(fd, errno);
#ifdef __APPLE__
    struct termios settings;
    if (tcgetattr(fd, &settings)) return fail_fd(fd, errno);
    cfmakeraw(&settings);
    if (tcsetattr(fd, TCSANOW, &settings)) return fail_fd(fd, errno);
#endif
    return fd;
}

static int open_directory(const char *path, int shared_radio)
{
    struct stat status;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -errno;
    if (fstat(fd, &status)) return fail_fd(fd, errno);
    if (shared_radio) {
        if (geteuid() != 1001 || status.st_uid != 1001 || status.st_gid != 1001 ||
            (status.st_mode & 07777) != 0770)
            return fail_fd(fd, EACCES);
    } else if (status.st_uid != geteuid() || status.st_mode & 0022) {
        return fail_fd(fd, EACCES);
    }
    return fd;
}

int mindone_mux_open_directory(const char *path)
{
    return open_directory(path, 0);
}

int mindone_mux_open_radio_directory(const char *path)
{
    return open_directory(path, 1);
}

int mindone_mux_open_pty(char *path, size_t capacity, int radio)
{
    int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -errno;
    if (grantpt(fd) || unlockpt(fd)) return fail_fd(fd, errno);
#ifdef __APPLE__
    char name[128];
    if (ioctl(fd, TIOCPTYGNAME, name)) return fail_fd(fd, errno);
    size_t length = strnlen(name, sizeof(name));
    if (length == sizeof(name) || length >= capacity) return fail_fd(fd, ENAMETOOLONG);
    memcpy(path, name, length + 1);
#else
    int error = ptsname_r(fd, path, capacity);
    if (error) return fail_fd(fd, error);
#endif
#ifndef __APPLE__
    struct termios settings;
    if (tcgetattr(fd, &settings)) return fail_fd(fd, errno);
    settings.c_iflag = IGNPAR;
    settings.c_oflag = 0;
    settings.c_cflag = CS8 | CREAD | CLOCAL;
    settings.c_lflag = 0;
    memset(settings.c_cc, 0, sizeof(settings.c_cc));
    settings.c_cc[VMIN] = 1;
    if (cfsetispeed(&settings, B460800) || cfsetospeed(&settings, B460800) ||
        tcflush(fd, TCIFLUSH) || tcsetattr(fd, TCSANOW, &settings))
        return fail_fd(fd, errno);
#endif
    if (radio && chown(path, 1001, 1001)) return fail_fd(fd, errno);
    if (chmod(path, radio ? 0660 : 0600)) return fail_fd(fd, errno);
    return fd;
}

int mindone_mux_publish(int directory, const char *name, const char *target,
                        struct mindone_mux_link_id *identity)
{
    struct stat status;
    if (symlinkat(target, directory, name)) {
        int error = errno;
        char temporary[256];
        if (error != EEXIST) return -error;
        if (fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW)) return -errno;
        if (!S_ISLNK(status.st_mode)) return -EEXIST;
        int length = snprintf(temporary, sizeof(temporary), ".%s.%ld", name, (long)getpid());
        if (length < 0 || (size_t)length >= sizeof(temporary)) return -ENAMETOOLONG;
        if (unlinkat(directory, temporary, 0) && errno != ENOENT) return -errno;
        if (symlinkat(target, directory, temporary)) return -errno;
        if (renameat(directory, temporary, directory, name)) {
            error = errno;
            unlinkat(directory, temporary, 0);
            return -error;
        }
    }
    if (fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW)) {
        int error = errno;
        char actual[256];
        ssize_t length = readlinkat(directory, name, actual, sizeof(actual));
        if (length >= 0 && (size_t)length == strlen(target) && !memcmp(actual, target, (size_t)length))
            unlinkat(directory, name, 0);
        return -error;
    }
    identity->device = status.st_dev;
    identity->inode = status.st_ino;
    return 0;
}

int mindone_mux_unpublish(int directory, const char *name, const char *target,
                          const struct mindone_mux_link_id *identity)
{
    struct stat status;
    char actual[256];
    if (fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW))
        return errno == ENOENT ? 0 : -errno;
    if (!S_ISLNK(status.st_mode) || identity->device != (uint64_t)status.st_dev ||
        identity->inode != (uint64_t)status.st_ino) return -ESTALE;
    ssize_t length = readlinkat(directory, name, actual, sizeof(actual));
    if (length < 0) return -errno;
    if ((size_t)length != strlen(target) || memcmp(actual, target, (size_t)length))
        return -ESTALE;
    return unlinkat(directory, name, 0) ? -errno : 0;
}

int mindone_mux_poll(struct mindone_mux_pollfd *entries, size_t count, int timeout)
{
    struct pollfd descriptors[47];
    if (count > 47 || timeout < 0 || timeout > 1000) return -EINVAL;
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
                            ((events & (POLLHUP | POLLERR)) ? 4U : 0U) |
                            ((events & POLLNVAL) ? 8U : 0U);
    }
    return 0;
}
