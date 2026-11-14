/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "FlipWatcher.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <log/log.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>
#include <utility>

namespace mindone::camera {
namespace {

constexpr char kKeypad[] = "mtk-kpd";
const std::string kNames[] = {"device@1.1/internal/0", "device@1.1/internal/1"};

int readActive(const std::string& path) {
    std::string value;
    if (!android::base::ReadFileToString(path, &value)) return -1;
    value = android::base::Trim(value);
    if (value == "1") return 0;
    if (value == "0") return 1;
    return -1;
}

android::base::unique_fd openKeypad(const std::string& dir) {
    std::unique_ptr<DIR, decltype(&closedir)> entries(opendir(dir.c_str()), closedir);
    if (!entries) return {};
    while (dirent* entry = readdir(entries.get())) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        std::string path = dir + "/" + entry->d_name;
        android::base::unique_fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
        if (fd.get() < 0) continue;
        char name[64] = {};
        if (ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name) < 0) continue;
        if (strcmp(name, kKeypad) == 0) return fd;
    }
    return {};
}

}

std::shared_ptr<FlipWatcher> FlipWatcher::create(const std::string& hallPath,
                                                 const std::string& inputDir, Hooks hooks) {
    android::base::unique_fd input = openKeypad(inputDir);
    if (input.get() < 0) {
        ALOGE("No %s input device in %s, both camera IDs stay present", kKeypad,
              inputDir.c_str());
        return nullptr;
    }
    return create(std::move(input), hallPath, std::move(hooks));
}

std::shared_ptr<FlipWatcher> FlipWatcher::create(android::base::unique_fd input,
                                                 const std::string& hallPath, Hooks hooks) {
    const int active = readActive(hallPath);
    if (active < 0) {
        ALOGE("No flip position in %s, both camera IDs stay present", hallPath.c_str());
        return nullptr;
    }
    std::shared_ptr<FlipWatcher> watcher(
            new FlipWatcher(std::move(input), hallPath, std::move(hooks)));
    watcher->apply(active);
    std::thread([watcher] { watcher->run(); }).detach();
    return watcher;
}

FlipWatcher::FlipWatcher(android::base::unique_fd input, std::string hallPath, Hooks hooks)
    : mInput(std::move(input)), mHallPath(std::move(hallPath)), mHooks(std::move(hooks)) {}

void FlipWatcher::attach() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mActive >= 0) mHooks.report(kNames[1 - mActive], false);
}

void FlipWatcher::apply(int active) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (active == mActive) return;
    if (active < 0) {
        const std::string hidden = kNames[1 - mActive];
        mActive = -1;
        mHooks.setAbsent({});
        mHooks.report(hidden, true);
        ALOGW("Flip exclusivity stopped, %s is present again", hidden.c_str());
        return;
    }
    const std::string& going = kNames[1 - active];
    const std::string& coming = kNames[active];
    const bool comingAbsent = mActive == 1 - active;
    mHooks.setAbsent({going, coming});
    mHooks.leave(going);
    mHooks.report(going, false);
    mActive = active;
    mHooks.setAbsent({going});
    if (comingAbsent) mHooks.report(coming, true);
    ALOGI("Flip position: %s present, %s not present", coming.c_str(), going.c_str());
}

void FlipWatcher::run() {
    input_event events[16];
    for (;;) {
        ssize_t bytes;
        do {
            bytes = read(mInput.get(), events, sizeof(events));
        } while (bytes < 0 && errno == EINTR);
        if (bytes <= 0 || bytes % sizeof(input_event) != 0) break;
        int keyed = -1;
        for (size_t i = 0; i < bytes / sizeof(input_event); ++i) {
            if (events[i].type != EV_KEY || events[i].value != 1) continue;
            if (events[i].code == KEY_F2) keyed = 0;
            if (events[i].code == KEY_F3) keyed = 1;
        }
        if (keyed < 0) continue;
        const int active = readActive(mHallPath);
        apply(active < 0 ? keyed : active);
    }
    ALOGE("Lost the %s input (%s), both camera IDs stay present", kKeypad, strerror(errno));
    apply(-1);
}

}
