// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#include <v4l2_codec2/v4l2/MdpReplay.h>

#include <cstdarg>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" int ioctl(int fd, int request, ...) {
    va_list ap;
    va_start(ap, request);
    void *argp = va_arg(ap, void *);
    va_end(ap);

    mdpreplay::MdpReplayEngine *engine = mdpreplay::ActiveRecordingEngine();
    if (engine) {
        if (static_cast<unsigned int>(request) == mdpreplay::kCmdqIoctlAsyncExec) {
            engine->OnAsyncExec(fd, argp);
        } else if (static_cast<unsigned int>(request) == mdpreplay::kCmdqIoctlAsyncWait) {
            engine->OnAsyncWait();
        }
    }

    return static_cast<int>(syscall(__NR_ioctl, fd, request, argp));
}
