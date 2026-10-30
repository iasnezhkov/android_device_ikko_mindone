/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
namespace ndk {
struct ICInterface { virtual ~ICInterface() = default; };
struct ScopedAStatus { bool isOk() const { return true; } std::string getDescription() const { return {}; } };
}
namespace mindone::radio {
enum class Service { Config, Data, Messaging, Modem, Network, Sim, Voice, Ims, Count };
struct Epoch { uint64_t generation; std::atomic<bool> active{true}; explicit Epoch(uint64_t value) : generation(value) {} };
struct Snapshot {
    std::shared_ptr<Epoch> epoch;
    std::shared_ptr<::ndk::ICInterface> indication;
    bool current() const { return epoch && epoch->active.load(); }
    uint64_t generation() const { return epoch ? epoch->generation : 0; }
};
inline std::vector<std::string> events;
}
