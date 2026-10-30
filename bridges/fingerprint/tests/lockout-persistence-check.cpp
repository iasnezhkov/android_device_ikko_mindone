// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

#include "LockoutTracker.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using mindone::LockoutStore;
using mindone::LockoutTracker;
using Callback = LockoutTracker::Callback;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::string root = argv[1];
    auto cb = std::make_shared<Callback>();
    {
        LockoutTracker tracker(10, root);
        assert(!tracker.notifyLockout(cb));
        for (int i = 0; i < 4; ++i) assert(!tracker.addFailedAttempt());
    }
    {
        LockoutTracker tracker(10, root);
        assert(tracker.addFailedAttempt());
        assert(tracker.notifyLockout(cb));
        assert(cb->timed > 0 && cb->timed <= 30000);
    }
    {
        LockoutTracker tracker(10, root);
        assert(tracker.notifyLockout(cb));
        assert(cb->timed > 0);
    }
    {
        LockoutTracker tracker(11, root);
        assert(!tracker.notifyLockout(cb));
    }
    {
        std::ofstream(root + "/10.lockout", std::ios::binary | std::ios::trunc) << "corrupt";
        LockoutTracker tracker(10, root);
        assert(tracker.notifyLockout(cb));
    }
    {
        std::filesystem::remove(root + "/11.lockout");
        LockoutTracker tracker(11, root);
        assert(tracker.notifyLockout(cb));
    }
    {
        LockoutStore store(12, root);
        assert(store.save({15, 5, nowMs() - 1000}));
        LockoutTracker tracker(12, root);
        assert(!tracker.notifyLockout(cb));
        for (int i = 0; i < 4; ++i) assert(!tracker.addFailedAttempt());
        assert(tracker.addFailedAttempt());
        int before = cb->permanent;
        assert(tracker.notifyLockout(cb));
        assert(cb->permanent == before + 1);
    }
    {
        LockoutTracker tracker(12, root);
        int before = cb->permanent;
        assert(tracker.notifyLockout(cb));
        assert(cb->permanent == before + 1);
        tracker.reset(cb);
        assert(!tracker.notifyLockout(cb));
    }
    {
        LockoutTracker tracker(12, root);
        assert(!tracker.notifyLockout(cb));
        LockoutStore::State state;
        LockoutStore store(12, root);
        assert(store.load(state));
        assert(state.failed == 0 && state.failedTimed == 0 && state.deadlineMs == 0);
        assert(std::filesystem::exists(root + "/12.seen"));
    }
    {
        std::filesystem::remove(root + "/12.lockout");
        LockoutTracker tracker(12, root);
        assert(tracker.notifyLockout(cb));
    }
    {
        LockoutStore store(13, root);
        assert(store.save({4, 4, 0}));
        std::fstream record(root + "/13.lockout", std::ios::in | std::ios::out | std::ios::binary);
        record.seekp(12);
        record.put(0);
        record.close();
        LockoutTracker tracker(13, root);
        assert(tracker.notifyLockout(cb));
    }
    {
        LockoutTracker tracker(14, root + "/absent");
        assert(tracker.notifyLockout(cb));
        tracker.reset(cb);
        assert(tracker.notifyLockout(cb));
    }
    {
        LockoutStore store(15, root);
        assert(store.save({20, 0, 0}));
        LockoutTracker tracker(15, root);
        std::filesystem::rename(root + "/15.seen", root + "/15.backup");
        std::filesystem::create_directory(root + "/15.seen");
        int errors = cb->errors;
        int cleared = cb->cleared;
        tracker.reset(cb);
        assert(cb->errors == errors + 1 && cb->cleared == cleared);
        int before = cb->permanent;
        assert(tracker.notifyLockout(cb));
        assert(cb->permanent == before + 1);
    }
    {
        LockoutStore store(16, root);
        assert(store.save({5, 5, nowMs() + 10000}));
        LockoutTracker tracker(16, root);
        assert(tracker.notifyLockout(cb));
        assert(cb->timed > 0 && cb->timed <= 10000);
        tracker.reset();
    }
    {
        LockoutTracker tracker(16, root);
        assert(!tracker.notifyLockout(cb));
    }
    {
        LockoutTracker tracker(15, root);
        int before = cb->permanent;
        assert(tracker.notifyLockout(cb));
        assert(cb->permanent == before + 1);
    }
    {
        LockoutStore store(17, root);
        assert(store.save({0, 0, 0}));
        std::fstream marker(root + "/17.seen", std::ios::in | std::ios::out | std::ios::binary);
        marker.put(0);
        marker.close();
        LockoutTracker tracker(17, root);
        assert(tracker.notifyLockout(cb));
    }
    {
        LockoutStore store(18, root);
        assert(store.save({4, 4, 0}));
        std::filesystem::remove(root + "/18.lockout");
        std::filesystem::create_directory(root + "/18.lockout");
        assert(!store.save({5, 5, nowMs() + 30000}));
        LockoutTracker tracker(18, root);
        assert(tracker.notifyLockout(cb));
    }
    {
        std::filesystem::copy_file(root + "/10.lockout", root + "/19.lockout");
        LockoutTracker tracker(19, root);
        assert(tracker.notifyLockout(cb));
    }
    std::cout << "PASS persistence: restart, realtime expiry, CRC, corruption, missing record, "
                 "user isolation, permanent, reset, storage failure\n";
}
