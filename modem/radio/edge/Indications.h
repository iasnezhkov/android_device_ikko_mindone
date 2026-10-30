/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include "Callbacks.h"
#include "mindone_radio.h"
#include <aidl/android/hardware/radio/modem/RadioState.h>
#include <aidl/android/hardware/radio/network/SignalStrength.h>
#include <functional>
#include <optional>
#include <mutex>
namespace mindone::radio {
class Indications {
public:
    void deliver(const mr_indication& indication, const std::function<Snapshot(Service)>& current);
    void initial(Service service, const Snapshot& snapshot);
private:
    void connected(const Snapshot& snapshot);
    std::mutex deliveryMutex_;
    std::mutex mutex_;
    bool connected_ = false;
    std::optional<uint64_t> connectedGeneration_;
    std::optional<uint64_t> resetGeneration_;
    ::aidl::android::hardware::radio::modem::RadioState radio_ = ::aidl::android::hardware::radio::modem::RadioState::UNAVAILABLE;
    std::optional<::aidl::android::hardware::radio::network::SignalStrength> signal_;
};
}
