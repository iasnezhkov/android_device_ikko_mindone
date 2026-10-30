/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Indications.h"
#include "Conversions.h"
#include <aidl/android/hardware/radio/RadioIndicationType.h>
#include <aidl/android/hardware/radio/modem/IRadioModemIndication.h>
#include <aidl/android/hardware/radio/network/IRadioNetworkIndication.h>
#include <aidl/android/hardware/radio/sim/IRadioSimIndication.h>
#include <android-base/logging.h>
#include <limits>
#include <string>

namespace mindone::radio {
namespace radio = ::aidl::android::hardware::radio;
constexpr auto unsolicited = radio::RadioIndicationType::UNSOLICITED;
template <typename Interface, typename Function>
void invoke(const Snapshot& snapshot, Function function) {
    if (!snapshot.current() || !snapshot.indication) return;
    auto status = function(std::static_pointer_cast<Interface>(snapshot.indication));
    if (!status.isOk()) LOG(ERROR) << "Radio indication delivery failed: " << status.getDescription();
}
void Indications::connected(const Snapshot& snapshot) {
    if (!connected_ || !snapshot.current() || !snapshot.indication || connectedGeneration_ == snapshot.generation()) return;
    connectedGeneration_ = snapshot.generation();
    invoke<radio::modem::IRadioModemIndication>(snapshot, [](auto callback) {
        return callback->rilConnected(unsolicited);
    });
}
void Indications::initial(Service service, const Snapshot& snapshot) {
    std::lock_guard delivery(deliveryMutex_);
    radio::modem::RadioState state;
    std::optional<radio::network::SignalStrength> signal;
    {
        std::lock_guard lock(mutex_);
        state = radio_;
        signal = signal_;
    }
    if (service == Service::Modem) {
        connected(snapshot);
        invoke<radio::modem::IRadioModemIndication>(snapshot, [state](auto callback) {
            return callback->radioStateChanged(unsolicited, state);
        });
    } else if (service == Service::Network) {
        invoke<radio::network::IRadioNetworkIndication>(snapshot, [](auto callback) {
            return callback->networkStateChanged(unsolicited);
        });
        if (signal) invoke<radio::network::IRadioNetworkIndication>(snapshot, [&signal](auto callback) {
            return callback->currentSignalStrength(unsolicited, *signal);
        });
    } else if (service == Service::Sim) {
        invoke<radio::sim::IRadioSimIndication>(snapshot, [](auto callback) {
            return callback->simStatusChanged(unsolicited);
        });
    }
}
void Indications::deliver(const mr_indication& indication, const std::function<Snapshot(Service)>& current) {
    std::lock_guard delivery(deliveryMutex_);
    const auto modem = current(Service::Modem);
    const auto network = current(Service::Network);
    const auto sim = current(Service::Sim);
    switch (indication.kind) {
        case MR_INDICATION_NETWORK_STATE_CHANGED:
            invoke<radio::network::IRadioNetworkIndication>(network, [](auto callback) {
                return callback->networkStateChanged(unsolicited);
            });
            break;
        case MR_INDICATION_CURRENT_SIGNAL_STRENGTH: {
            radio::network::SignalStrength signal;
            if (!convert::signal(indication.signal, signal)) return;
            {
                std::lock_guard lock(mutex_);
                signal_ = signal;
            }
            invoke<radio::network::IRadioNetworkIndication>(network, [&signal](auto callback) {
                return callback->currentSignalStrength(unsolicited, signal);
            });
            break;
        }
        case MR_INDICATION_RADIO_STATE_CHANGED: {
            radio::modem::RadioState state;
            switch (indication.radio_state) {
                case MR_RADIO_UNAVAILABLE: state = radio::modem::RadioState::UNAVAILABLE; break;
                case MR_RADIO_OFF: state = radio::modem::RadioState::OFF; break;
                case MR_RADIO_ON: state = radio::modem::RadioState::ON; break;
                default: LOG(ERROR) << "Invalid runtime radio state"; return;
            }
            if (state == radio::modem::RadioState::UNAVAILABLE) {
                connected_ = false;
                connectedGeneration_.reset();
            } else {
                connected_ = true;
                connected(modem);
            }
            {
                std::lock_guard lock(mutex_);
                if (radio_ == state) return;
                radio_ = state;
            }
            invoke<radio::modem::IRadioModemIndication>(modem, [state](auto callback) {
                return callback->radioStateChanged(unsolicited, state);
            });
            break;
        }
        case MR_INDICATION_SIM_STATUS_CHANGED:
            invoke<radio::sim::IRadioSimIndication>(sim, [](auto callback) {
                return callback->simStatusChanged(unsolicited);
            });
            break;
        case MR_INDICATION_MODEM_RESET:
            if (resetGeneration_ && indication.generation <= *resetGeneration_) return;
            resetGeneration_ = indication.generation;
            connected_ = false;
            connectedGeneration_.reset();
            {
                std::lock_guard lock(mutex_);
                signal_.reset();
                radio_ = radio::modem::RadioState::UNAVAILABLE;
            }
            invoke<radio::modem::IRadioModemIndication>(modem, [&indication](auto callback) {
                return callback->modemReset(unsolicited, "runtime modem reset; cause unavailable; generation " + std::to_string(indication.generation));
            });
            invoke<radio::modem::IRadioModemIndication>(modem, [](auto callback) {
                return callback->radioStateChanged(unsolicited, radio::modem::RadioState::UNAVAILABLE);
            });
            break;
        default: LOG(ERROR) << "Unknown runtime indication " << indication.kind;
    }
}
}
