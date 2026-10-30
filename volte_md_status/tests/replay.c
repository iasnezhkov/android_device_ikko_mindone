/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PROP_VALUE_MAX 92
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_ERROR 6
#define _IO(type, number) (((type) << 8) | (number))
#define open fake_open
#define close fake_close
#define read fake_read
#define ioctl fake_ioctl
#define sleep fake_sleep

static jmp_buf finished;
static int32_t event_kind;

static int __android_log_print(int priority, const char *tag, const char *format, ...) {
    (void)priority;
    (void)tag;
    (void)format;
    return 0;
}

static int fake_open(const char *path, int flags) {
    (void)path;
    (void)flags;
    int byte = getchar();
    if (byte == EOF) longjmp(finished, 1);
    ungetc(byte, stdin);
    puts("open");
    return 17;
}

static int fake_close(int fd) {
    if (fd != 17) abort();
    puts("close");
    return 0;
}

static int fake_ioctl(int fd, unsigned long request, int arg) {
    if (fd != 17 || arg != -1) abort();
    printf("ioctl %lu\n", request);
    return 0;
}

static unsigned int fake_sleep(unsigned int seconds) {
    printf("sleep %u\n", seconds);
    return 0;
}

static ssize_t fake_read(int fd, void *buffer, size_t size) {
    int32_t count;
    if (fd != 17 || size != 56 || fread(&count, 4, 1, stdin) != 1) abort();
    printf("read %d\n", count);
    if (count < 0) {
        errno = -count;
        return -1;
    }
    if ((size_t)count > size || fread(buffer, 1, count, stdin) != (size_t)count) abort();
    if (count == 56) memcpy(&event_kind, (char *)buffer + 20, sizeof(event_kind));
    return count;
}

static int __system_property_set(const char *name, const char *value) {
    if (strcmp(name, "vendor.volte_md_status")) abort();
    printf("publish %d %s\n", event_kind, value);
    return 0;
}

static int __system_property_get(const char *name, char *value) {
    (void)name;
    value[0] = '\0';
    return 0;
}

@SOURCE@

_Static_assert(sizeof(struct md_status_event) == 56, "event size");
_Static_assert(_Alignof(struct md_status_event) == 8, "event alignment");
_Static_assert(offsetof(struct md_status_event, event_type) == 20, "event offset");
_Static_assert(offsetof(struct md_status_event, reason) == 24, "reason offset");

int main(void) {
    if (setjmp(finished) == 0) status_main();
    return 0;
}
