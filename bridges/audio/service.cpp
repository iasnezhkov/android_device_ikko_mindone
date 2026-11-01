/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define LOG_TAG "AHAL_Mindone"
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <system/audio_config.h>

#include "Backend.h"
#include "ModuleMindone.h"
#include "SoundTriggerMindone.h"
#include "core-impl/AudioPolicyConfigXmlConverter.h"
#include "core-impl/ChildInterface.h"
#include "core-impl/Config.h"
#include "core-impl/Module.h"
#include "effectFactory-impl/EffectFactory.h"

using aidl::android::hardware::audio::core::ChildInterface;
using aidl::android::hardware::audio::core::Config;
using aidl::android::hardware::audio::core::Module;
using aidl::android::hardware::audio::core::internal::AudioPolicyConfigXmlConverter;
using aidl::android::hardware::audio::core::mindone::LegacyBackend;
using aidl::android::hardware::audio::effect::Factory;

namespace {

std::vector<ChildInterface<Module>> registerModules(const std::shared_ptr<LegacyBackend>& backend,
                                                    AudioPolicyConfigXmlConverter& converter) {
    std::vector<ChildInterface<Module>> instances;
    auto configs = converter.releaseModuleConfigs();
    if (configs == nullptr) {
        LOG(ERROR) << "No audio module configurations available";
        return instances;
    }
    for (auto& [name, config] : *configs) {
        auto type = Module::typeFromString(name);
        if (!type.has_value()) {
            LOG(ERROR) << "Unsupported audio module type " << name;
            continue;
        }
        std::shared_ptr<Module> module;
        if (name == "default") {
            module = ndk::SharedRefBase::make<
                    aidl::android::hardware::audio::core::mindone::ModuleMindone>(
                    std::move(config), backend);
        } else {
            module = Module::createInstance(*type, std::move(config));
        }
        if (module == nullptr) {
            LOG(ERROR) << "Failed to create audio module " << name;
            continue;
        }
        ChildInterface<Module> instance;
        instance = std::move(module);
        const std::string moduleName = std::string(Module::descriptor) + "/" + name;
        if (AServiceManager_addService(instance.getBinder(), moduleName.c_str()) != STATUS_OK) {
            LOG(ERROR) << "Failed to register service " << moduleName;
            continue;
        }
        instances.push_back(std::move(instance));
    }
    return instances;
}

}

int main() {
    auto backend = LegacyBackend::open();
    if (backend == nullptr) {
        LOG(ERROR) << "Failed to initialize legacy audio backend";
        return EXIT_FAILURE;
    }

    std::srand(std::time(nullptr));
    ::android::base::SetMinimumLogSeverity(::android::base::DEBUG);
    ABinderProcess_setThreadPoolMaxThreadCount(16);
    ABinderProcess_startThreadPool();
    LOG(INFO) << "Init for Mindone Audio AIDL HAL";

    AudioPolicyConfigXmlConverter audioPolicyConverter{
            "/vendor/etc/audio_policy_configuration.xml"};
    auto config = ndk::SharedRefBase::make<Config>(audioPolicyConverter);
    const std::string configName = std::string(Config::descriptor) + "/default";
    if (AServiceManager_addService(config->asBinder().get(), configName.c_str()) != STATUS_OK) {
        LOG(ERROR) << "Failed to register service " << configName;
        return EXIT_FAILURE;
    }

    const auto effectConfigFile =
            ::android::audio_find_readable_configuration_file("audio_effects_config.xml");
    if (effectConfigFile.empty()) {
        LOG(ERROR) << "Audio effects configuration audio_effects_config.xml not found";
        return EXIT_FAILURE;
    }
    auto effectFactory = ndk::SharedRefBase::make<Factory>(effectConfigFile);
    const std::string effectFactoryName = std::string(Factory::descriptor) + "/default";
    if (AServiceManager_addService(effectFactory->asBinder().get(), effectFactoryName.c_str()) !=
        STATUS_OK) {
        LOG(ERROR) << "Failed to register service " << effectFactoryName;
        return EXIT_FAILURE;
    }

    auto moduleInstances = registerModules(backend, audioPolicyConverter);
    auto soundTrigger = ndk::SharedRefBase::make<
            aidl::android::hardware::soundtrigger3::mindone::SoundTriggerMindone>();
    if (!soundTrigger->initialize()) {
        LOG(ERROR) << "Legacy soundtrigger backend unavailable, ISoundTriggerHw reports no device";
    }
    const std::string soundTriggerName =
            std::string(aidl::android::hardware::soundtrigger3::ISoundTriggerHw::descriptor) +
            "/default";
    if (AServiceManager_addService(soundTrigger->asBinder().get(), soundTriggerName.c_str()) !=
        STATUS_OK) {
        LOG(ERROR) << "Failed to register service " << soundTriggerName;
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
