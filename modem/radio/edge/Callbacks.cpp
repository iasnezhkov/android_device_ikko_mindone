/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Callbacks.h"
#include <android-base/logging.h>
#include <limits>
#include <utility>

namespace mindone::radio {
struct DeathCookie {
    std::weak_ptr<CallbackState> state;
    uint64_t generation;
};
class DeathGroup {
public:
    DeathGroup() : recipient_(AIBinder_DeathRecipient_new([](void* data) {
        auto* cookie = static_cast<DeathCookie*>(data);
        const auto generation = cookie->generation;
        if (auto state = cookie->state.lock()) state->invalidate(generation);
    })) {
        CHECK(recipient_);
        AIBinder_DeathRecipient_setOnUnlinked(recipient_, [](void* data) {
            delete static_cast<DeathCookie*>(data);
        });
    }
    ~DeathGroup() { AIBinder_DeathRecipient_delete(recipient_); }
    void link(const std::shared_ptr<::ndk::ICInterface>& callback,
              const std::shared_ptr<CallbackState>& state, uint64_t generation) {
        if (!callback) return;
        auto binder = callback->asBinder();
        if (!binder.get() || !AIBinder_isRemote(binder.get())) return;
        auto* cookie = new DeathCookie{state, generation};
        if (AIBinder_linkToDeath(binder.get(), recipient_, cookie) != STATUS_OK) {
            state->invalidate(generation);
        }
    }
private:
    AIBinder_DeathRecipient* recipient_;
};
CallbackState::CallbackState(Service service, Invalidated invalidated)
    : service_(service), invalidated_(std::move(invalidated)) {}
CallbackState::~CallbackState() {
    if (snapshot_.epoch) snapshot_.epoch->active.store(false, std::memory_order_release);
}
Snapshot CallbackState::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
Snapshot CallbackState::replace(std::shared_ptr<::ndk::ICInterface> response,
                                std::shared_ptr<::ndk::ICInterface> indication) {
    auto group = std::make_shared<DeathGroup>();
    std::shared_ptr<DeathGroup> retired;
    Snapshot installed;
    uint64_t previous;
    {
        std::lock_guard lock(mutex_);
        CHECK_NE(generation_, std::numeric_limits<uint64_t>::max());
        previous = generation_;
        if (snapshot_.epoch) snapshot_.epoch->active.store(false, std::memory_order_release);
        installed = {std::make_shared<Epoch>(++generation_), std::move(response), std::move(indication)};
        if (!installed.response || !installed.indication) installed.epoch->active.store(false);
        snapshot_ = installed;
        retired = std::move(deaths_);
        deaths_ = group;
    }
    retired.reset();
    invalidated_(service_, previous);
    if (installed.current()) {
        group->link(installed.response, shared_from_this(), installed.generation());
        group->link(installed.indication, shared_from_this(), installed.generation());
    }
    return installed;
}
void CallbackState::invalidate(uint64_t generation) {
    std::shared_ptr<DeathGroup> retired;
    {
        std::lock_guard lock(mutex_);
        if (!snapshot_.epoch || snapshot_.generation() != generation) return;
        CHECK_NE(generation_, std::numeric_limits<uint64_t>::max());
        snapshot_.epoch->active.store(false, std::memory_order_release);
        ++generation_;
        snapshot_ = {};
        retired = std::move(deaths_);
    }
    invalidated_(service_, generation);
    retired.reset();
}
}
