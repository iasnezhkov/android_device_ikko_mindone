/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include "route-edge.h"
#include <cassert>
namespace mindone::radio {
class Edge {
public:
    Snapshot snapshot;
    std::shared_ptr<::ndk::ICInterface> indication;
    int calls = 0;
    void replace(Service service, std::shared_ptr<::ndk::ICInterface> response,
                 std::shared_ptr<::ndk::ICInterface> nextIndication) {
        assert(service == Service::Ims);
        snapshot.response = std::move(response);
        indication = std::move(nextIndication);
    }
    ::ndk::ScopedAStatus request(Service service, Method, int32_t serial,
                                std::any, Reply fallback) {
        assert(service == Service::Ims);
        ++calls;
        ResponseInfo info{};
        info.serial = serial;
        info.error = RadioError::REQUEST_NOT_SUPPORTED;
        return fallback(snapshot, info, nullptr);
    }
};
}
