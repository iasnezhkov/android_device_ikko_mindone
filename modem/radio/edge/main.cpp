/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Edge.h"
#include "generated/RadioConfig.h"
#include "generated/RadioData.h"
#include "generated/RadioMessaging.h"
#include "generated/RadioModem.h"
#include "generated/RadioNetwork.h"
#include "generated/RadioSim.h"
#include "generated/RadioVoice.h"
#include "generated/RadioIms.h"
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <cstdlib>
#include <vector>

namespace mindone::radio {
template <typename Interface>
void publish(const std::shared_ptr<Edge>& edge, const char* instance,
             std::vector<std::shared_ptr<::ndk::ICInterface>>& services) {
    const auto name = std::string(Interface::descriptor) + "/" + instance;
    CHECK(AServiceManager_isDeclared(name.c_str())) << name;
    auto service = ::ndk::SharedRefBase::make<Interface>(edge);
    CHECK_EQ(AServiceManager_addService(service->asBinder().get(), name.c_str()), STATUS_OK) << name;
    services.push_back(std::move(service));
}
}
int main(int argc, char** argv) {
    static_cast<void>(argc);
    android::base::InitLogging(argv, android::base::LogdLogger(android::base::RADIO));
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    auto edge = std::make_shared<mindone::radio::Edge>(mindone::radio::makeRoutes());
    CHECK(edge->start());
    std::vector<std::shared_ptr<::ndk::ICInterface>> services;
    mindone::radio::publish<mindone::radio::RadioConfig>(edge, "default", services);
    mindone::radio::publish<mindone::radio::RadioData>(edge, "slot1", services);
    mindone::radio::publish<mindone::radio::RadioMessaging>(edge, "slot1", services);
    mindone::radio::publish<mindone::radio::RadioModem>(edge, "slot1", services);
    mindone::radio::publish<mindone::radio::RadioNetwork>(edge, "slot1", services);
    mindone::radio::publish<mindone::radio::RadioSim>(edge, "slot1", services);
    mindone::radio::publish<mindone::radio::RadioVoice>(edge, "slot1", services);
    mindone::radio::publish<mindone::radio::RadioIms>(edge, "slot1", services);
    ABinderProcess_joinThreadPool();
    edge->stop();
    return EXIT_FAILURE;
}
