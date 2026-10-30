/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Edge.h"
#include "Indications.h"
#include <aidl/android/hardware/radio/RadioResponseType.h>
#include <android-base/logging.h>
#include <limits>
#include <utility>

namespace mindone::radio {
Edge::Edge(Routes routes) : routes_(std::move(routes)), indications_(std::make_unique<Indications>()) {}
Edge::~Edge() { stop(); }
bool Edge::start() {
    std::lock_guard lifecycle(lifecycle_);
    if (runtime_ || stopping_.load(std::memory_order_acquire)) return false;
    if (mr_abi_version() != MR_ABI_VERSION || mr_create(&runtime_) != MR_OK) return false;
    for (size_t index = 0; index < callbacks_.size(); ++index) {
        callbacks_[index] = std::make_shared<CallbackState>(static_cast<Service>(index),
            [owner = weak_from_this()](Service service, uint64_t generation) {
                if (auto edge = owner.lock()) edge->discard(service, generation);
            });
    }
    localWorker_ = std::thread([this] { localDispatch(); });
    dispatcher_ = std::thread([this] { dispatch(); });
    return true;
}
void Edge::stop() {
    std::lock_guard lifecycle(lifecycle_);
    mr_runtime runtime;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
        if (!runtime) return;
        stopping_.store(true, std::memory_order_release);
        initials_.clear();
        for (const auto& [id, invocation] : invocations_) {
            static_cast<void>(invocation);
            completions_.push_back({id, RadioError::RADIO_NOT_AVAILABLE});
        }
    }
    localChanged_.notify_one();
    mr_stop(runtime);
    if (dispatcher_.joinable()) dispatcher_.join();
    {
        std::lock_guard lock(mutex_);
        localStopping_ = true;
    }
    localChanged_.notify_one();
    if (localWorker_.joinable()) localWorker_.join();
    {
        std::lock_guard lock(mutex_);
        runtime_ = 0;
    }
    mr_destroy(&runtime);
}
void Edge::replace(Service service, std::shared_ptr<::ndk::ICInterface> response,
                   std::shared_ptr<::ndk::ICInterface> indication) {
    auto snapshot = callbacks_[static_cast<size_t>(service)]->replace(std::move(response), std::move(indication));
    std::lock_guard lock(mutex_);
    if (snapshot.current() && !stopping_.load(std::memory_order_acquire)) {
        initials_.push_back({service, std::move(snapshot)});
        localChanged_.notify_one();
    }
}
void Edge::acknowledge(Service service) {
    static_cast<void>(service);
}
void Edge::discard(Service service, uint64_t generation) {
    std::lock_guard lock(mutex_);
    for (auto it = invocations_.begin(); it != invocations_.end();) {
        if (it->second.service == service && it->second.snapshot.generation() == generation) {
            it = invocations_.erase(it);
        } else {
            ++it;
        }
    }
}
RadioError Edge::mapStatus(int32_t status) {
    switch (status) {
        case MR_INVALID_ARGUMENT: return RadioError::INVALID_ARGUMENTS;
        case MR_STOPPED: case MR_IO_ERROR: case MR_INVALID_HANDLE: return RadioError::RADIO_NOT_AVAILABLE;
        default: return RadioError::INTERNAL_ERR;
    }
}
::ndk::ScopedAStatus Edge::request(Service service, Method method, int32_t serial,
                                  std::any arguments, Reply fallback) {
    auto snapshot = callbacks_[static_cast<size_t>(service)]->snapshot();
    if (!snapshot.current() || !snapshot.response) return ::ndk::ScopedAStatus::ok();
    const auto route = routes_.find(std::make_pair(service, method));
    uint64_t id;
    mr_runtime runtime;
    {
        std::lock_guard lock(mutex_);
        if (!snapshot.current()) return ::ndk::ScopedAStatus::ok();
        if (localStopping_) return ::ndk::ScopedAStatus::fromStatus(STATUS_DEAD_OBJECT);
        runtime = runtime_;
        CHECK_NE(nextInvocation_, std::numeric_limits<uint64_t>::max());
        id = ++nextInvocation_;
        invocations_.emplace(id, Invocation{service, method, serial, snapshot,
            route != routes_.end() && route->second.reply ? route->second.reply : std::move(fallback)});
        if (stopping_.load(std::memory_order_acquire)) {
            completions_.push_back({id, RadioError::RADIO_NOT_AVAILABLE});
            localChanged_.notify_one();
            return ::ndk::ScopedAStatus::ok();
        }
        if (route == routes_.end() || !route->second.submit) {
            completions_.push_back({id, RadioError::REQUEST_NOT_SUPPORTED});
            localChanged_.notify_one();
            return ::ndk::ScopedAStatus::ok();
        }
        const int32_t status = route->second.submit(runtime, id, arguments);
        if (status != MR_OK) {
            completions_.push_back({id, mapStatus(status)});
            localChanged_.notify_one();
        }
    }
    return ::ndk::ScopedAStatus::ok();
}
void Edge::complete(uint64_t id, RadioError error, const mr_value* value) {
    Invocation invocation;
    {
        std::lock_guard lock(mutex_);
        auto node = invocations_.extract(id);
        if (node.empty()) return;
        invocation = std::move(node.mapped());
        if (stopping_.load(std::memory_order_acquire)) {
            error = RadioError::RADIO_NOT_AVAILABLE;
            value = nullptr;
        }
    }
    if (!invocation.snapshot.current()) return;
    ResponseInfo info;
    info.type = ::aidl::android::hardware::radio::RadioResponseType::SOLICITED;
    info.serial = invocation.serial;
    info.error = error;
    auto status = invocation.reply(invocation.snapshot, info, value);
    if (status.getStatus() == STATUS_DEAD_OBJECT) {
        callbacks_[static_cast<size_t>(invocation.service)]->invalidate(invocation.snapshot.generation());
    } else if (!status.isOk()) {
        LOG(ERROR) << "Radio response delivery failed: " << status.getDescription();
    }
}
void Edge::consume(mr_batch batch) {
    uint32_t count = 0;
    CHECK_EQ(mr_batch_count(batch, &count), MR_OK);
    const auto current = [this](Service service) { return callbacks_[static_cast<size_t>(service)]->snapshot(); };
    for (uint32_t index = 0; index < count; ++index) {
        const mr_output* output = nullptr;
        CHECK_EQ(mr_batch_at(batch, index, &output), MR_OK);
        if (output->kind == MR_OUTPUT_RESPONSE) {
            complete(output->invocation_id, static_cast<RadioError>(output->error), output->value_present ? &output->value : nullptr);
        } else if (output->kind == MR_OUTPUT_INDICATION) {
            indications_->deliver(output->indication, current);
        } else {
            LOG(ERROR) << "Unknown runtime output kind " << output->kind;
        }
    }
    CHECK_EQ(mr_batch_free(&batch), MR_OK);
}
void Edge::localDispatch() {
    while (true) {
        std::deque<Completion> completions;
        std::deque<Initial> initials;
        {
            std::unique_lock lock(mutex_);
            localChanged_.wait(lock, [this] {
                return localStopping_ || !completions_.empty() || !initials_.empty();
            });
            if (localStopping_ && completions_.empty() && initials_.empty()) return;
            completions.swap(completions_);
            initials.swap(initials_);
        }
        for (const auto& completion : completions) complete(completion.id, completion.error, nullptr);
        for (const auto& initial : initials) {
            if (!stopping_.load(std::memory_order_acquire)) indications_->initial(initial.service, initial.snapshot);
        }
    }
}
void Edge::dispatch() {
    while (true) {
        mr_batch batch = 0;
        const int32_t status = mr_next_batch(runtime_, MR_WAIT_FOREVER, &batch);
        if (status == MR_OK) {
            consume(batch);
            continue;
        }
        if (status != MR_STOPPED) LOG(ERROR) << "Radio output wait failed: " << status;
        {
            std::lock_guard lock(mutex_);
            stopping_.store(true, std::memory_order_release);
            initials_.clear();
            for (const auto& [id, invocation] : invocations_) {
                static_cast<void>(invocation);
                completions_.push_back({id, RadioError::RADIO_NOT_AVAILABLE});
            }
        }
        localChanged_.notify_one();
        if (status != MR_STOPPED) mr_stop(runtime_);
        return;
    }
}
}
