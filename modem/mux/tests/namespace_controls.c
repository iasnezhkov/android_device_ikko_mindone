/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uid_t effective = 1001;
static uid_t owner = 1001;
static gid_t group = 1001;
static int stat_failure;
static int opened = -1;
static int passes;

static int observed_open(const char *path, int flags, ...)
{
    opened = open(path, flags);
    return opened;
}

static uid_t observed_euid(void)
{
    return effective;
}

static int observed_stat(int fd, struct stat *status)
{
    if (stat_failure) {
        errno = EIO;
        return -1;
    }
    int result = fstat(fd, status);
    if (!result) {
        status->st_uid = owner;
        status->st_gid = group;
    }
    return result;
}

#define open observed_open
#define geteuid observed_euid
#define fstat observed_stat
#include POSIX_SOURCE
#undef open
#undef geteuid
#undef fstat

static void check(const char *name, int result, int expected)
{
    if ((expected == 0 && result < 0) || (expected != 0 && result != -expected)) {
        fprintf(stderr, "%s result=%d expected=%d\n", name, result, expected);
        exit(1);
    }
    if (result >= 0) {
        if (!(fcntl(result, F_GETFD) & FD_CLOEXEC)) {
            fprintf(stderr, "directory descriptor is missing CLOEXEC\n");
            exit(2);
        }
        struct stat status;
        if (fstat(result, &status) || !S_ISDIR(status.st_mode)) exit(3);
        if (close(result)) exit(4);
    } else if (opened >= 0 && (fcntl(opened, F_GETFD) != -1 || errno != EBADF)) {
        fprintf(stderr, "%s leaked directory descriptor\n", name);
        exit(5);
    }
    printf("PASS %s\n", name);
    passes++;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 64;
    const char *directory = argv[1];
    if (chmod(directory, 0700)) return 65;
    check("private owner-only accepted", mindone_mux_open_directory(directory), 0);
    if (chmod(directory, 0770)) return 66;
    check("private current init mode rejected", mindone_mux_open_directory(directory), EACCES);
#ifdef BASELINE
    printf("BASELINE_MISMATCH_PROVEN tests=%d\n", passes);
#else
    check("shared exact radio identity mode accepted", mindone_mux_open_radio_directory(directory), 0);
    effective = 0;
    check("shared root caller rejected", mindone_mux_open_radio_directory(directory), EACCES);
    effective = 1002;
    check("shared unrelated caller rejected", mindone_mux_open_radio_directory(directory), EACCES);
    effective = 1001;
    owner = 0;
    check("shared other owner rejected", mindone_mux_open_radio_directory(directory), EACCES);
    owner = 1001;
    group = 0;
    check("shared other group rejected", mindone_mux_open_radio_directory(directory), EACCES);
    group = 1001;
    const mode_t modes[] = { 0777, 0772, 0700, 0750, 02770, 01770, 04770 };
    for (size_t index = 0; index < sizeof(modes)/sizeof(modes[0]); ++index) {
        if (chmod(directory, modes[index])) return 67;
        char name[96];
        snprintf(name, sizeof(name), "shared unsupported mode %04o rejected", modes[index]);
        check(name, mindone_mux_open_radio_directory(directory), EACCES);
    }
    if (chmod(directory, 0770)) return 68;
    stat_failure = 1;
    check("shared fstat failure closes descriptor", mindone_mux_open_radio_directory(directory), EIO);
    stat_failure = 0;
    char path[1024];
    snprintf(path, sizeof(path), "%s/link", directory);
    if (symlink(directory, path)) return 69;
    int result = mindone_mux_open_radio_directory(path);
    if (result >= 0) {
        fprintf(stderr, "shared symlink unexpectedly accepted\n");
        return 70;
    }
    check("shared symlink rejected", result, -result);
    if (unlink(path)) return 71;
    snprintf(path, sizeof(path), "%s/file", directory);
    int file = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (file < 0 || close(file)) return 72;
    result = mindone_mux_open_radio_directory(path);
    if (result >= 0) return 73;
    check("shared regular file rejected", result, -result);
    if (unlink(path)) return 74;
    if (chmod(directory, 0750)) return 75;
    check("private group readable accepted", mindone_mux_open_directory(directory), 0);
    if (chmod(directory, 0777)) return 76;
    check("private world writable remains rejected", mindone_mux_open_directory(directory), EACCES);
    printf("NAMESPACE_CONTROLS_PASS tests=%d\n", passes);
#endif
    return 0;
}
