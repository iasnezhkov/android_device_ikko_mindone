/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

static pthread_t worker;
static pthread_t owner;
static struct sigaction previous;
static volatile sig_atomic_t delivered;

static void on_signal(int signal)
{
    (void)signal;
    delivered = 1;
}

static void *interrupt_owner(void *unused)
{
    (void)unused;
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 20000000 };
    nanosleep(&delay, NULL);
    pthread_kill(owner, SIGUSR1);
    return NULL;
}

int mindone_mux_test_interrupt_begin(void)
{
    struct sigaction action = { 0 };
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    delivered = 0;
    if (sigaction(SIGUSR1, &action, &previous)) return -1;
    owner = pthread_self();
    int error = pthread_create(&worker, NULL, interrupt_owner, NULL);
    if (error) sigaction(SIGUSR1, &previous, NULL);
    return error;
}

int mindone_mux_test_interrupt_end(void)
{
    pthread_join(worker, NULL);
    sigaction(SIGUSR1, &previous, NULL);
    return delivered;
}

int mindone_mux_test_errno(int kind)
{
    const int errors[] = { EINTR, EAGAIN, EBUSY, ENODEV, EIO, EBADF, ETXTBSY };
    return kind >= 0 && kind < 7 ? errors[kind] : EINVAL;
}
