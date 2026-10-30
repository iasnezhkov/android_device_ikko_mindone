/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#endif
static volatile sig_atomic_t stopped;
static void stop(int number) { stopped = number; }
int mindone_mux_signal(void) { return stopped; }
int mindone_mux_initialize(void)
{
    stopped = 0;
    struct sigaction action = { 0 };
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) return -errno;
    action.sa_handler = SIG_IGN;
    const int ignored[] = { SIGPIPE, SIGHUP, SIGUSR1, SIGUSR2 };
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++)
        if (sigaction(ignored[i], &action, NULL)) return -errno;
    umask(0);
    return 0;
}
int mindone_mux_drop_root(void)
{
#ifdef __ANDROID__
    if (getuid() == 0) {
        prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);
        if (setuid(1001)) return -errno;
    }
#endif
    return 0;
}
int mindone_mux_get_property(const char *key, char *value)
{
#ifdef __ANDROID__
    return __system_property_get(key, value);
#else
    (void)key;
    value[0] = 0;
    return 0;
#endif
}
void mindone_mux_log(int priority, const char *message)
{
#ifdef __ANDROID__
    __android_log_write(priority, "mindone_mux", message);
#else
    (void)priority;
    (void)message;
#endif
}
