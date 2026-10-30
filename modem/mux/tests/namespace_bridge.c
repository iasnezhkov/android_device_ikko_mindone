/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uid_t fixture_euid(void)
{
    return 1001;
}

static int fixture_stat(int fd, struct stat *status)
{
    int result = fstat(fd, status);
    if (!result) {
        status->st_uid = 1001;
        status->st_gid = 1001;
    }
    return result;
}

#define geteuid fixture_euid
#define fstat fixture_stat
#define mindone_mux_open_radio_directory fixture_radio_directory
#include POSIX_SOURCE
#undef geteuid
#undef fstat
#undef mindone_mux_open_radio_directory

int mindone_mux_open_radio_directory(const char *path)
{
    const char *root = getenv("MINDONE_NAMESPACE_TEST_DIRECTORY");
    if (!root || strcmp(path, "/dev/radio")) return -EINVAL;
    return fixture_radio_directory(root);
}
