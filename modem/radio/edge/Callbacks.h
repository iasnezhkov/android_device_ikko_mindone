/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include <android/binder_interface_utils.h>
#include <android/binder_ibinder.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace mindone::radio {
enum class Service : uint32_t { Config, Data, Messaging, Modem, Network, Sim, Voice, Ims, Count };
struct Epoch {
    explicit Epoch(uint64_t value) : generation(value) {}
    const uint64_t generation;
    std::atomic<bool> active{true};
};
struct Snapshot {
    std::shared_ptr<Epoch> epoch;
    std::shared_ptr<::ndk::ICInterface> response;
    std::shared_ptr<::ndk::ICInterface> indication;
    bool current() const { return epoch && epoch->active.load(std::memory_order_acquire); }
    uint64_t generation() const { return epoch ? epoch->generation : 0; }
};
class DeathGroup;
class CallbackState : public std::enable_shared_from_this<CallbackState> {
public:
    using Invalidated = std::function<void(Service, uint64_t)>;
    CallbackState(Service service, Invalidated invalidated);
    ~CallbackState();
    Snapshot replace(std::shared_ptr<::ndk::ICInterface> response,
                     std::shared_ptr<::ndk::ICInterface> indication);
    Snapshot snapshot() const;
    void invalidate(uint64_t generation);
private:
    Service service_;
    Invalidated invalidated_;
    mutable std::mutex mutex_;
    uint64_t generation_ = 0;
    Snapshot snapshot_;
    std::shared_ptr<DeathGroup> deaths_;
};
}
