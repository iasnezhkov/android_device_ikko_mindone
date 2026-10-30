/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Indications.h"
#include <aidl/android/hardware/radio/modem/IRadioModemIndication.h>
#include <aidl/android/hardware/radio/network/IRadioNetworkIndication.h>
#include <aidl/android/hardware/radio/sim/IRadioSimIndication.h>
#include <cassert>
#include <iostream>
using namespace mindone::radio;
namespace r = aidl::android::hardware::radio;
int main() {
    Indications indications;
    std::array<Snapshot,static_cast<size_t>(Service::Count)> snapshots;
    auto& modem=snapshots[static_cast<size_t>(Service::Modem)];
    auto& network=snapshots[static_cast<size_t>(Service::Network)];
    auto& sim=snapshots[static_cast<size_t>(Service::Sim)];
    modem={std::make_shared<Epoch>(1),std::make_shared<r::modem::IRadioModemIndication>()};
    network={std::make_shared<Epoch>(1),std::make_shared<r::network::IRadioNetworkIndication>()};
    sim={std::make_shared<Epoch>(1),std::make_shared<r::sim::IRadioSimIndication>()};
    const auto current=[&snapshots](Service service) { return snapshots[static_cast<size_t>(service)]; };
    indications.initial(Service::Modem,modem);
    assert(events==std::vector<std::string>{"state:1"}); events.clear();
    mr_indication input{}; input.kind=MR_INDICATION_RADIO_STATE_CHANGED; input.radio_state=MR_RADIO_OFF;
    indications.deliver(input,current);
    assert((events==std::vector<std::string>{"connected","state:0"})); events.clear();
    indications.initial(Service::Modem,modem);
    assert(events==std::vector<std::string>{"state:0"}); events.clear();
    input.radio_state=MR_RADIO_ON; indications.deliver(input,current);
    assert(events==std::vector<std::string>{"state:10"}); events.clear();
    input.kind=MR_INDICATION_MODEM_RESET; input.generation=7; indications.deliver(input,current);
    assert((events==std::vector<std::string>{"reset","state:1"})); events.clear();
    indications.deliver(input,current); assert(events.empty());
    input.generation=6; indications.deliver(input,current); assert(events.empty());
    input.kind=MR_INDICATION_RADIO_STATE_CHANGED; input.radio_state=MR_RADIO_UNAVAILABLE;
    indications.deliver(input,current); assert(events.empty());
    indications.initial(Service::Modem,modem); assert(events==std::vector<std::string>{"state:1"}); events.clear();
    input.radio_state=MR_RADIO_OFF; indications.deliver(input,current);
    assert((events==std::vector<std::string>{"connected","state:0"})); events.clear();
    auto old=modem; old.epoch->active=false;
    modem={std::make_shared<Epoch>(2),std::make_shared<r::modem::IRadioModemIndication>()};
    indications.initial(Service::Modem,old); assert(events.empty());
    indications.initial(Service::Modem,modem);
    assert((events==std::vector<std::string>{"connected","state:0"})); events.clear();
    input.kind=MR_INDICATION_SIM_STATUS_CHANGED; indications.deliver(input,current);
    assert(events==std::vector<std::string>{"sim"}); events.clear();
    sim.epoch->active=false; indications.deliver(input,current); assert(events.empty());
    input.kind=MR_INDICATION_NETWORK_STATE_CHANGED; indications.deliver(input,current);
    assert(events==std::vector<std::string>{"network"}); events.clear();
    input.kind=MR_INDICATION_CURRENT_SIGNAL_STRENGTH; indications.deliver(input,current);
    assert(events==std::vector<std::string>{"signal:2147483647"}); events.clear();
    network.epoch->active=false; indications.deliver(input,current); assert(events.empty());
    auto replaced=modem; replaced.epoch->active=false;
    modem={std::make_shared<Epoch>(3),std::make_shared<r::modem::IRadioModemIndication>()};
    indications.initial(Service::Modem,modem);
    assert((events==std::vector<std::string>{"connected","state:0"})); events.clear();
    input.kind=MR_INDICATION_RADIO_STATE_CHANGED; input.radio_state=MR_RADIO_ON; indications.deliver(input,current);
    assert(events==std::vector<std::string>{"state:10"}); events.clear();
    std::cout << "indication state, reconnect, generation and sentinel fixtures passed\n";
}
