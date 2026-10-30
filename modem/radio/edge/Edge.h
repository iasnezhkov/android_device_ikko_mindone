/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include "Callbacks.h"
#include "generated/Methods.h"
#include "mindone_radio.h"
#include <aidl/android/hardware/radio/RadioError.h>
#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <any>
#include <array>
#include <atomic>
#include <deque>
#include <condition_variable>
#include <map>
#include <thread>

namespace mindone::radio {
using ResponseInfo = ::aidl::android::hardware::radio::RadioResponseInfo;
using RadioError = ::aidl::android::hardware::radio::RadioError;
using Reply = std::function<::ndk::ScopedAStatus(const Snapshot&, const ResponseInfo&, const mr_value*)>;
using Submit = std::function<int32_t(mr_runtime, uint64_t, const std::any&)>;
struct Route { Submit submit; Reply reply; };
using Routes = std::map<std::pair<Service, Method>, Route>;
class Indications;
class Edge : public std::enable_shared_from_this<Edge> {
public:
    explicit Edge(Routes routes);
    ~Edge();
    bool start();
    void stop();
    void replace(Service service, std::shared_ptr<::ndk::ICInterface> response,
                 std::shared_ptr<::ndk::ICInterface> indication);
    void acknowledge(Service service);
    ::ndk::ScopedAStatus request(Service service, Method method, int32_t serial,
                                std::any arguments, Reply fallback);
private:
    struct Invocation {
        Service service;
        Method method;
        int32_t serial;
        Snapshot snapshot;
        Reply reply;
    };
    struct Completion { uint64_t id; RadioError error; };
    struct Initial { Service service; Snapshot snapshot; };
    void discard(Service service, uint64_t generation);
    void complete(uint64_t id, RadioError error, const mr_value* value);
    void dispatch();
    void localDispatch();
    void consume(mr_batch batch);
    static RadioError mapStatus(int32_t status);
    const Routes routes_;
    std::array<std::shared_ptr<CallbackState>, static_cast<size_t>(Service::Count)> callbacks_;
    mr_runtime runtime_ = 0;
    std::thread dispatcher_;
    std::thread localWorker_;
    std::condition_variable localChanged_;
    bool localStopping_ = false;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::mutex lifecycle_;
    uint64_t nextInvocation_ = 0;
    std::map<uint64_t, Invocation> invocations_;
    std::deque<Completion> completions_;
    std::deque<Initial> initials_;
    std::unique_ptr<Indications> indications_;
};
Routes makeRoutes(uint32_t uiccQueryKind = MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED);
}
