/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Dumpstate.h"

#include <android-base/properties.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <string>
#include <thread>

namespace aidl::android::hardware::dumpstate {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr char kVerboseProperty[] = "persist.vendor.dumpstate.verbose_logging.enabled";
constexpr auto kTotalBudget = 30s;
constexpr auto kSectionBudget = 5s;

struct Section {
    const char* title;
    const char* command;
    bool verboseOnly;
};

constexpr Section kSections[] = {
        {"Kernel version and modules", "cat /proc/version /proc/modules", false},
        {"Thermal zones",
         "for d in /sys/class/thermal/thermal_zone*; do "
         "for f in \"$d/type\" \"$d/temp\"; do "
         "if [ -r \"$f\" ]; then printf '\\n%s\\n' \"$f\"; cat \"$f\"; fi; done; done",
         false},
        {"Power supply",
         "for f in /sys/devices/platform/charger/power_supply/*/uevent "
         "/sys/devices/platform/eta6965_chg/power_supply/*/uevent "
         "/sys/devices/platform/soc/10026000.pwrap/10026000.pwrap:mt6366/"
         "mtk-charger-detection/power_supply/*/uevent; do "
         "if [ -r \"$f\" ]; then printf '\\n%s\\n' \"$f\"; "
         "sed '/^POWER_SUPPLY_SERIAL_NUMBER=/d' \"$f\"; fi; done",
         false},
        {"GPU frequency summary",
         "for f in /proc/gpufreqv2/gpufreq_status /proc/gpufreqv2/gpufreq_opp_dump; do "
         "if [ -f \"$f\" ] && [ -r \"$f\" ]; then "
         "printf '\\n%s\\n' \"$f\"; cat \"$f\"; fi; done",
         false},
        {"GPU GED",
         "for f in /proc/ged /proc/ged/*; do "
         "if [ -f \"$f\" ] && [ -r \"$f\" ]; then "
         "printf '\\n%s\\n' \"$f\"; cat \"$f\"; fi; done",
         true},
        {"Pstore names",
         "for f in /sys/fs/pstore/*; do "
         "if [ -e \"$f\" ]; then printf '%s\\n' \"${f##*/}\"; fi; done",
         false},
        {"Modem CCCI status",
         "for f in /sys/kernel/ccci/*; do "
         "if [ -f \"$f\" ] && [ -r \"$f\" ]; then "
         "printf '\\n%s\\n' \"$f\"; cat \"$f\"; fi; done",
         true},
};

bool RunSection(int fd, const Section& section, bool verbose, Clock::time_point deadline) {
    std::string script = "export PATH=/vendor/bin; (printf '\\n------ ";
    script += section.title;
    script += " ------\\n'; ";
    script += section.verboseOnly && !verbose ? "printf 'Verbose logging disabled\\n'"
                                              : section.command;
    script += ") 2>&1 | head -c 65536";

    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        if (setpgid(0, 0) != 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (fd > STDERR_FILENO) {
            close(fd);
        }
        execl("/vendor/bin/sh", "sh", "-c", script.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    setpgid(pid, pid);
    const auto sectionDeadline = std::min(deadline, Clock::now() + kSectionBudget);
    while (Clock::now() < sectionDeadline) {
        const pid_t result = waitpid(pid, nullptr, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) {
            kill(-pid, SIGKILL);
            return true;
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        std::this_thread::sleep_for(
                std::min<Clock::duration>(10ms, sectionDeadline - Clock::now()));
    }
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    pid_t result;
    do {
        result = waitpid(pid, nullptr, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        std::thread([pid] {
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
            }
        }).detach();
    }
    return true;
}

}

ndk::ScopedAStatus Dumpstate::dumpstateBoard(const std::vector<ndk::ScopedFileDescriptor>& in_fds,
                                             IDumpstateDevice::DumpstateMode in_mode,
                                             int64_t in_timeoutMillis) {
    const auto started = Clock::now();
    if (in_fds.empty() || in_fds.size() > 2 || in_timeoutMillis < 0) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "Invalid descriptors or timeout");
    }
    for (const auto& fd : in_fds) {
        const int flags = fcntl(fd.get(), F_GETFL);
        if (flags < 0 || (flags & O_ACCMODE) == O_RDONLY || (flags & O_PATH) != 0) {
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "Descriptor is not writable");
        }
    }
    switch (in_mode) {
        case IDumpstateDevice::DumpstateMode::FULL:
        case IDumpstateDevice::DumpstateMode::INTERACTIVE:
        case IDumpstateDevice::DumpstateMode::REMOTE:
        case IDumpstateDevice::DumpstateMode::DEFAULT:
            break;
        case IDumpstateDevice::DumpstateMode::WEAR:
        case IDumpstateDevice::DumpstateMode::CONNECTIVITY:
        case IDumpstateDevice::DumpstateMode::WIFI:
        case IDumpstateDevice::DumpstateMode::PROTO:
            return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                    ERROR_UNSUPPORTED_MODE, "Unsupported dumpstate mode");
        default:
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "Invalid dumpstate mode");
    }

    const auto budget =
            std::min(std::chrono::milliseconds(in_timeoutMillis),
                     std::chrono::duration_cast<std::chrono::milliseconds>(kTotalBudget));
    const auto deadline = started + budget;
    const bool verbose = ::android::base::GetBoolProperty(kVerboseProperty, false);
    for (const auto& section : kSections) {
        if (Clock::now() >= deadline) {
            break;
        }
        if (!RunSection(in_fds[0].get(), section, verbose, deadline)) {
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_STATE,
                                                                    "Cannot start diagnostics");
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Dumpstate::getVerboseLoggingEnabled(bool* _aidl_return) {
    *_aidl_return = ::android::base::GetBoolProperty(kVerboseProperty, false);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Dumpstate::setVerboseLoggingEnabled(bool in_enable) {
    if (!::android::base::SetProperty(kVerboseProperty, in_enable ? "true" : "false")) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_STATE,
                                                                "Cannot persist verbose logging");
    }
    return ndk::ScopedAStatus::ok();
}

}
