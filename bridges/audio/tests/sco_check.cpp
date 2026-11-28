/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cassert>
#include <cerrno>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "BluetoothMindone.h"
#include "ModuleMindone.h"
#include "StreamMindone.h"

extern "C" {
void sco_test_reset(void);
void sco_test_set_in_call(bool in_call, bool bt_sco_voice);
void sco_test_set_change_error(int err);
bool sco_test_bt_wb(void);
bool sco_test_out_reopen_pending(void);
bool sco_test_other_out_reopen_pending(void);
bool sco_test_in_reopen_pending(void);
void sco_test_clear_reopen_flags(void);
unsigned int sco_test_voice_change_count(void);
unsigned int sco_test_open_count(void);
unsigned int sco_test_close_count(void);
}

namespace aidl::android::hardware::audio::core::mindone {

DriverMindone::DriverMindone(StreamContext& context, bool isInput,
                             std::shared_ptr<LegacyBackend> backend)
    : mBackend(std::move(backend)), mDriverContext(context), mIsInput(isInput) {}

::android::status_t DriverMindone::init(DriverCallbackInterface*) { return ::android::OK; }
::android::status_t DriverMindone::drain(StreamDescriptor::DrainMode) { return ::android::OK; }
::android::status_t DriverMindone::flush() { return ::android::OK; }
::android::status_t DriverMindone::pause() { return ::android::OK; }
::android::status_t DriverMindone::standby() { return ::android::OK; }
::android::status_t DriverMindone::start() { return ::android::OK; }
::android::status_t DriverMindone::transfer(void*, size_t, size_t*, int32_t*) {
    return ::android::OK;
}
::android::status_t DriverMindone::refinePosition(StreamDescriptor::Position*) {
    return ::android::OK;
}
void DriverMindone::shutdown() {}

StreamInMindone::StreamInMindone(
        StreamContext&& context,
        const ::aidl::android::hardware::audio::common::SinkMetadata&,
        const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>&,
        std::shared_ptr<LegacyBackend> backend)
    : DriverMindone(context, true, std::move(backend)) {}

StreamInMindone::~StreamInMindone() {}

ndk::ScopedAStatus StreamInMindone::setConnectedDevices(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>&) {
    return ndk::ScopedAStatus::ok();
}

StreamOutMindone::StreamOutMindone(
        StreamContext&& context,
        const ::aidl::android::hardware::audio::common::SourceMetadata&,
        const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&,
        std::shared_ptr<LegacyBackend> backend)
    : DriverMindone(context, false, std::move(backend)) {}

StreamOutMindone::~StreamOutMindone() {}

ndk::ScopedAStatus StreamOutMindone::setConnectedDevices(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>&) {
    return ndk::ScopedAStatus::ok();
}

}

using aidl::android::hardware::audio::core::IBluetooth;
using aidl::android::hardware::audio::core::Module;
using aidl::android::hardware::audio::core::mindone::BluetoothMindone;
using aidl::android::hardware::audio::core::mindone::LegacyBackend;
using aidl::android::hardware::audio::core::mindone::ModuleMindone;
using aidl::android::media::audio::common::Boolean;
using aidl::android::media::audio::common::Float;
using aidl::android::media::audio::common::Int;

using ScoConfig = IBluetooth::ScoConfig;
using HfpConfig = IBluetooth::HfpConfig;

int main() {
    sco_test_reset();
    {
        auto backend = LegacyBackend::open();
        assert(backend != nullptr);
        assert(sco_test_open_count() == 1);

        auto config = std::make_unique<Module::Configuration>();
        std::shared_ptr<Module> module =
                ndk::SharedRefBase::make<ModuleMindone>(std::move(config), backend);

        auto nullStatus = module->getBluetooth(nullptr);
        assert(!nullStatus.isOk());
        assert(nullStatus.getExceptionCode() == EX_ILLEGAL_ARGUMENT);

        std::shared_ptr<IBluetooth> bt1;
        std::shared_ptr<IBluetooth> bt2;
        assert(module->getBluetooth(&bt1).isOk());
        assert(bt1 != nullptr);
        assert(module->getBluetooth(&bt2).isOk());
        assert(bt1 == bt2);

        ScoConfig applied;
        assert(bt1->setScoConfig(ScoConfig{}, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO);
        assert(applied.isEnabled.has_value() && !applied.isEnabled->value);
        assert(applied.isNrecEnabled.has_value() && !applied.isNrecEnabled->value);
        assert(!sco_test_bt_wb());
        assert(!sco_test_out_reopen_pending());
        assert(!sco_test_in_reopen_pending());
        assert(sco_test_voice_change_count() == 0);

        ScoConfig wbReq;
        wbReq.mode = ScoConfig::Mode::SCO_WB;
        assert(bt1->setScoConfig(wbReq, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO_WB);
        assert(sco_test_bt_wb());
        assert(sco_test_out_reopen_pending());
        assert(!sco_test_other_out_reopen_pending());
        assert(sco_test_in_reopen_pending());
        assert(sco_test_voice_change_count() == 0);

        sco_test_clear_reopen_flags();
        ScoConfig partial;
        partial.isEnabled = Boolean{true};
        partial.isNrecEnabled = Boolean{true};
        partial.debugName = std::string("HFP-Test");
        assert(bt1->setScoConfig(partial, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO_WB);
        assert(applied.isEnabled.has_value() && applied.isEnabled->value);
        assert(applied.isNrecEnabled.has_value() && applied.isNrecEnabled->value);
        assert(applied.debugName.has_value() && *applied.debugName == "HFP-Test");
        assert(sco_test_bt_wb());
        assert(!sco_test_out_reopen_pending());
        assert(!sco_test_in_reopen_pending());

        ScoConfig nbReq;
        nbReq.mode = ScoConfig::Mode::SCO;
        assert(bt1->setScoConfig(nbReq, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO);
        assert(!sco_test_bt_wb());
        assert(sco_test_out_reopen_pending());
        assert(!sco_test_other_out_reopen_pending());
        assert(sco_test_in_reopen_pending());
        assert(sco_test_voice_change_count() == 0);

        sco_test_clear_reopen_flags();
        sco_test_set_in_call(true, true);
        assert(bt1->setScoConfig(wbReq, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO_WB);
        assert(sco_test_bt_wb());
        assert(sco_test_voice_change_count() == 1);
        assert(sco_test_out_reopen_pending());
        assert(sco_test_in_reopen_pending());

        sco_test_clear_reopen_flags();
        assert(bt1->setScoConfig(wbReq, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO_WB);
        assert(sco_test_voice_change_count() == 1);
        assert(!sco_test_out_reopen_pending());
        assert(!sco_test_in_reopen_pending());

        assert(bt1->setScoConfig(nbReq, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO);
        assert(!sco_test_bt_wb());
        assert(sco_test_voice_change_count() == 2);

        ScoConfig swbReq;
        swbReq.mode = ScoConfig::Mode::SCO_SWB;
        swbReq.isEnabled = Boolean{false};
        auto swbStatus = bt1->setScoConfig(swbReq, &applied);
        assert(!swbStatus.isOk());
        assert(swbStatus.getExceptionCode() == EX_UNSUPPORTED_OPERATION);
        assert(!sco_test_bt_wb());
        assert(sco_test_voice_change_count() == 2);
        assert(bt1->setScoConfig(ScoConfig{}, &applied).isOk());
        assert(applied.isEnabled.has_value() && applied.isEnabled->value);
        assert(applied.mode == ScoConfig::Mode::SCO);

        ScoConfig badMode;
        badMode.mode = static_cast<ScoConfig::Mode>(99);
        auto badModeStatus = bt1->setScoConfig(badMode, &applied);
        assert(!badModeStatus.isOk());
        assert(badModeStatus.getExceptionCode() == EX_ILLEGAL_ARGUMENT);

        auto nullResult = bt1->setScoConfig(wbReq, nullptr);
        assert(!nullResult.isOk());
        assert(nullResult.getExceptionCode() == EX_ILLEGAL_ARGUMENT);

        sco_test_set_change_error(-ETIMEDOUT);
        auto errStatus = bt1->setScoConfig(wbReq, &applied);
        assert(!errStatus.isOk());
        assert(errStatus.getExceptionCode() == EX_ILLEGAL_STATE);
        assert(sco_test_voice_change_count() == 3);
        assert(bt1->setScoConfig(ScoConfig{}, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO);

        sco_test_set_change_error(0);
        assert(bt1->setScoConfig(wbReq, &applied).isOk());
        assert(applied.mode == ScoConfig::Mode::SCO_WB);
        assert(sco_test_bt_wb());

        HfpConfig hfpApplied;
        assert(bt1->setHfpConfig(HfpConfig{}, &hfpApplied).isOk());
        assert(hfpApplied.isEnabled.has_value() && !hfpApplied.isEnabled->value);
        assert(hfpApplied.sampleRate.has_value() && hfpApplied.sampleRate->value == 8000);
        assert(hfpApplied.volume.has_value() && hfpApplied.volume->value == HfpConfig::VOLUME_MAX);

        HfpConfig hfpReq;
        hfpReq.isEnabled = Boolean{true};
        hfpReq.sampleRate = Int{16000};
        hfpReq.volume = Float{0.5f};
        assert(bt1->setHfpConfig(hfpReq, &hfpApplied).isOk());
        assert(hfpApplied.isEnabled.has_value() && hfpApplied.isEnabled->value);
        assert(hfpApplied.sampleRate.has_value() && hfpApplied.sampleRate->value == 16000);
        assert(hfpApplied.volume.has_value() && hfpApplied.volume->value == 0.5f);

        for (int32_t badRate : {0, -1, -8000}) {
            HfpConfig bad;
            bad.sampleRate = Int{badRate};
            auto st = bt1->setHfpConfig(bad, &hfpApplied);
            assert(!st.isOk() && st.getExceptionCode() == EX_ILLEGAL_ARGUMENT);
        }
        for (float badVol : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity()}) {
            HfpConfig bad;
            bad.volume = Float{badVol};
            auto st = bt1->setHfpConfig(bad, &hfpApplied);
            assert(!st.isOk() && st.getExceptionCode() == EX_ILLEGAL_ARGUMENT);
        }
        assert(bt1->setHfpConfig(HfpConfig{}, nullptr).getExceptionCode() == EX_ILLEGAL_ARGUMENT);

        BluetoothMindone detached(nullptr);
        auto detachedStatus = detached.setScoConfig(wbReq, &applied);
        assert(!detachedStatus.isOk());
        assert(detachedStatus.getExceptionCode() == EX_ILLEGAL_STATE);
    }
    assert(sco_test_close_count() == 1);
    std::cout << "PASS bridges/audio SCO wiring: getBluetooth, setScoConfig WB/NB pre-call & "
                 "in-call, reopen flags, partial/error/SWB guards, setHfpConfig\n";
    return 0;
}
