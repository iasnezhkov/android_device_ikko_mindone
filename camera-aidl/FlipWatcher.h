/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android-base/unique_fd.h>

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace mindone::camera {

class FlipWatcher {
  public:
    struct Hooks {
        std::function<void(const std::set<std::string>& absent)> setAbsent;
        std::function<void(const std::string& name)> leave;
        std::function<bool(const std::string& name, bool present)> report;
    };

    static std::shared_ptr<FlipWatcher> create(const std::string& hallPath,
                                               const std::string& inputDir, Hooks hooks);
    static std::shared_ptr<FlipWatcher> create(android::base::unique_fd input,
                                               const std::string& hallPath, Hooks hooks);

    void attach();

  private:
    FlipWatcher(android::base::unique_fd input, std::string hallPath, Hooks hooks);
    void apply(int active);
    void run();

    const android::base::unique_fd mInput;
    const std::string mHallPath;
    const Hooks mHooks;
    std::mutex mMutex;
    int mActive = -1;
};

}
