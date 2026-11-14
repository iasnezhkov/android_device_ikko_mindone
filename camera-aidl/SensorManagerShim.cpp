/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include <aidl/android/frameworks/sensorservice/BnEventQueueCallback.h>
#include <aidl/android/frameworks/sensorservice/IEventQueue.h>
#include <aidl/android/frameworks/sensorservice/ISensorManager.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/frameworks/sensorservice/1.0/ISensorManager.h>
#include <log/log.h>

#include <dlfcn.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mindone::camera {
namespace {

namespace hs = ::android::frameworks::sensorservice::V1_0;
namespace as = ::aidl::android::frameworks::sensorservice;
namespace hw = ::android::hardware::sensors::V1_0;
namespace aw = ::aidl::android::hardware::sensors;
using ::android::sp;
using ::android::wp;
using ::android::hardware::hidl_death_recipient;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

constexpr char kSwitch[] = "persist.vendor.camera.gyro_shim";
constexpr char kInstance[] = "android.frameworks.sensorservice.ISensorManager/default";
constexpr char kHidlLibrary[] = "android.frameworks.sensorservice@1.0.so";
constexpr char kGetService[] =
        "_ZN7android10frameworks13sensorservice4V1_014ISensorManager10getServiceERKNSt3__1"
        "12basic_stringIcNS4_11char_traitsIcEENS4_9allocatorIcEEEEb";

hs::Result result(const ndk::ScopedAStatus& status) {
    if (status.isOk()) return hs::Result::OK;
    switch (status.getExceptionCode()) {
        case EX_SERVICE_SPECIFIC:
            switch (status.getServiceSpecificError()) {
                case as::ISensorManager::RESULT_NOT_EXIST:
                    return hs::Result::NOT_EXIST;
                case as::ISensorManager::RESULT_NO_MEMORY:
                    return hs::Result::NO_MEMORY;
                case as::ISensorManager::RESULT_NO_INIT:
                    return hs::Result::NO_INIT;
                case as::ISensorManager::RESULT_PERMISSION_DENIED:
                    return hs::Result::PERMISSION_DENIED;
                case as::ISensorManager::RESULT_BAD_VALUE:
                    return hs::Result::BAD_VALUE;
                case as::ISensorManager::RESULT_INVALID_OPERATION:
                    return hs::Result::INVALID_OPERATION;
                default:
                    return hs::Result::UNKNOWN_ERROR;
            }
        case EX_SECURITY:
            return hs::Result::PERMISSION_DENIED;
        case EX_ILLEGAL_ARGUMENT:
            return hs::Result::BAD_VALUE;
        case EX_UNSUPPORTED_OPERATION:
            return hs::Result::INVALID_OPERATION;
        default:
            return hs::Result::UNKNOWN_ERROR;
    }
}

hw::SensorInfo convert(const aw::SensorInfo& in) {
    hw::SensorInfo out;
    out.sensorHandle = in.sensorHandle;
    out.name = in.name;
    out.vendor = in.vendor;
    out.version = in.version;
    out.type = static_cast<hw::SensorType>(in.type);
    out.typeAsString = in.typeAsString;
    out.maxRange = in.maxRange;
    out.resolution = in.resolution;
    out.power = in.power;
    out.minDelay = in.minDelayUs;
    out.fifoReservedEventCount = static_cast<uint32_t>(in.fifoReservedEventCount);
    out.fifoMaxEventCount = static_cast<uint32_t>(in.fifoMaxEventCount);
    out.requiredPermission = in.requiredPermission;
    out.maxDelay = in.maxDelayUs;
    out.flags = static_cast<uint32_t>(in.flags);
    return out;
}

template <size_t N, class Source, class Target>
void copyValues(const Source& source, Target* target) {
    for (size_t i = 0; i < N; ++i) (*target)[i] = source[i];
}

bool convert(const aw::Event& in, hw::Event* out) {
    using Tag = aw::Event::EventPayload::Tag;
    out->timestamp = in.timestamp;
    out->sensorHandle = in.sensorHandle;
    out->sensorType = static_cast<hw::SensorType>(in.sensorType);
    const auto& payload = in.payload;
    switch (payload.getTag()) {
        case Tag::vec3: {
            const auto& value = payload.get<Tag::vec3>();
            out->u.vec3.x = value.x;
            out->u.vec3.y = value.y;
            out->u.vec3.z = value.z;
            out->u.vec3.status = static_cast<hw::SensorStatus>(value.status);
            return true;
        }
        case Tag::vec4: {
            const auto& value = payload.get<Tag::vec4>();
            out->u.vec4.x = value.x;
            out->u.vec4.y = value.y;
            out->u.vec4.z = value.z;
            out->u.vec4.w = value.w;
            return true;
        }
        case Tag::uncal: {
            const auto& value = payload.get<Tag::uncal>();
            out->u.uncal.x = value.x;
            out->u.uncal.y = value.y;
            out->u.uncal.z = value.z;
            out->u.uncal.x_bias = value.xBias;
            out->u.uncal.y_bias = value.yBias;
            out->u.uncal.z_bias = value.zBias;
            return true;
        }
        case Tag::meta:
            out->u.meta.what = static_cast<hw::MetaDataEventType>(payload.get<Tag::meta>().what);
            return true;
        case Tag::scalar:
            out->u.scalar = payload.get<Tag::scalar>();
            return true;
        case Tag::stepCount:
            out->u.stepCount = static_cast<uint64_t>(payload.get<Tag::stepCount>());
            return true;
        case Tag::heartRate:
            out->u.heartRate.bpm = payload.get<Tag::heartRate>().bpm;
            out->u.heartRate.status =
                    static_cast<hw::SensorStatus>(payload.get<Tag::heartRate>().status);
            return true;
        case Tag::pose6DOF:
            copyValues<15>(payload.get<Tag::pose6DOF>().values, &out->u.pose6DOF);
            return true;
        case Tag::data:
            copyValues<16>(payload.get<Tag::data>().values, &out->u.data);
            return true;
        default:
            return false;
    }
}

class EventCallback : public as::BnEventQueueCallback {
  public:
    explicit EventCallback(const sp<hs::IEventQueueCallback>& callback) : mCallback(callback) {}

    ndk::ScopedAStatus onEvent(const aw::Event& event) override {
        hw::Event converted{};
        if (!convert(event, &converted)) {
            ALOGW("Dropping sensor %d event with payload tag %d", event.sensorHandle,
                  static_cast<int>(event.payload.getTag()));
            return ndk::ScopedAStatus::ok();
        }
        auto delivered = mCallback->onEvent(converted);
        if (!delivered.isOk()) ALOGW("HIDL sensor callback failed: %s",
                                     delivered.description().c_str());
        return ndk::ScopedAStatus::ok();
    }

  private:
    const sp<hs::IEventQueueCallback> mCallback;
};

class EventQueue : public hs::IEventQueue {
  public:
    explicit EventQueue(std::shared_ptr<as::IEventQueue> queue) : mQueue(std::move(queue)) {}

    Return<hs::Result> enableSensor(int32_t sensorHandle, int32_t samplingPeriodUs,
                                    int64_t maxBatchReportLatencyUs) override {
        return result(mQueue->enableSensor(sensorHandle, samplingPeriodUs,
                                           maxBatchReportLatencyUs));
    }

    Return<hs::Result> disableSensor(int32_t sensorHandle) override {
        return result(mQueue->disableSensor(sensorHandle));
    }

  private:
    const std::shared_ptr<as::IEventQueue> mQueue;
};

class Recipients {
  public:
    void setManager(const wp<hs::ISensorManager>& manager) {
        std::lock_guard<std::mutex> lock(mMutex);
        mManager = manager;
    }

    void add(const sp<hidl_death_recipient>& recipient, uint64_t cookie) {
        std::lock_guard<std::mutex> lock(mMutex);
        mEntries.emplace_back(recipient, cookie);
    }

    bool remove(const sp<hidl_death_recipient>& recipient) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto it = mEntries.begin(); it != mEntries.end(); ++it) {
            if (it->first == recipient) {
                mEntries.erase(it);
                return true;
            }
        }
        return false;
    }

    void died() {
        std::vector<std::pair<sp<hidl_death_recipient>, uint64_t>> entries;
        wp<hs::ISensorManager> manager;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            entries.swap(mEntries);
            manager = mManager;
        }
        ALOGW("%s died, notifying %zu HIDL clients", kInstance, entries.size());
        for (const auto& [recipient, cookie] : entries) recipient->serviceDied(cookie, manager);
    }

  private:
    std::mutex mMutex;
    wp<hs::ISensorManager> mManager;
    std::vector<std::pair<sp<hidl_death_recipient>, uint64_t>> mEntries;
};

class SensorManager : public hs::ISensorManager {
  public:
    explicit SensorManager(std::shared_ptr<as::ISensorManager> manager)
        : mManager(std::move(manager)),
          mRecipients(std::make_shared<Recipients>()),
          mDeath(AIBinder_DeathRecipient_new(&SensorManager::onDied)) {}

    void watch() {
        mRecipients->setManager(this);
        AIBinder_DeathRecipient_setOnUnlinked(mDeath.get(), &SensorManager::onUnlinked);
        auto* cookie = new std::shared_ptr<Recipients>(mRecipients);
        if (AIBinder_linkToDeath(mManager->asBinder().get(), mDeath.get(), cookie) != STATUS_OK) {
            ALOGW("Cannot watch %s for death", kInstance);
            delete cookie;
        }
    }

    Return<void> getSensorList(getSensorList_cb callback) override {
        std::vector<aw::SensorInfo> list;
        auto status = mManager->getSensorList(&list);
        hidl_vec<hw::SensorInfo> converted;
        if (status.isOk()) {
            converted.resize(list.size());
            for (size_t i = 0; i < list.size(); ++i) converted[i] = convert(list[i]);
        }
        callback(converted, result(status));
        return Void();
    }

    Return<void> getDefaultSensor(hw::SensorType type, getDefaultSensor_cb callback) override {
        aw::SensorInfo info;
        auto status = mManager->getDefaultSensor(static_cast<aw::SensorType>(type), &info);
        callback(status.isOk() ? convert(info) : hw::SensorInfo{}, result(status));
        return Void();
    }

    Return<void> createAshmemDirectChannel(const ::android::hardware::hidl_memory&, uint64_t,
                                           createAshmemDirectChannel_cb callback) override {
        callback(nullptr, hs::Result::INVALID_OPERATION);
        return Void();
    }

    Return<void> createGrallocDirectChannel(const ::android::hardware::hidl_handle&, uint64_t,
                                            createGrallocDirectChannel_cb callback) override {
        callback(nullptr, hs::Result::INVALID_OPERATION);
        return Void();
    }

    Return<void> createEventQueue(const sp<hs::IEventQueueCallback>& callback,
                                  createEventQueue_cb receive) override {
        if (!callback) {
            receive(nullptr, hs::Result::BAD_VALUE);
            return Void();
        }
        std::shared_ptr<as::IEventQueue> queue;
        auto status = mManager->createEventQueue(
                ndk::SharedRefBase::make<EventCallback>(callback), &queue);
        if (!status.isOk() || !queue) {
            receive(nullptr, status.isOk() ? hs::Result::UNKNOWN_ERROR : result(status));
            return Void();
        }
        receive(new EventQueue(std::move(queue)), hs::Result::OK);
        return Void();
    }

    Return<bool> linkToDeath(const sp<hidl_death_recipient>& recipient, uint64_t cookie) override {
        if (!recipient) return false;
        mRecipients->add(recipient, cookie);
        return true;
    }

    Return<bool> unlinkToDeath(const sp<hidl_death_recipient>& recipient) override {
        return mRecipients->remove(recipient);
    }

  private:
    static void onDied(void* cookie) {
        (*static_cast<std::shared_ptr<Recipients>*>(cookie))->died();
    }

    static void onUnlinked(void* cookie) {
        delete static_cast<std::shared_ptr<Recipients>*>(cookie);
    }

    const std::shared_ptr<as::ISensorManager> mManager;
    const std::shared_ptr<Recipients> mRecipients;
    ndk::ScopedAIBinder_DeathRecipient mDeath;
};

sp<hs::ISensorManager> createSensorManager() {
    ndk::SpAIBinder binder(AServiceManager_checkService(kInstance));
    auto manager = as::ISensorManager::fromBinder(binder);
    if (!manager) {
        ALOGE("%s is not available, the HIDL sensor manager stays unserved", kInstance);
        return nullptr;
    }
    sp<SensorManager> shim = new SensorManager(std::move(manager));
    shim->watch();
    ALOGI("HIDL ISensorManager/default is served by %s", kInstance);
    return shim;
}

sp<hs::ISensorManager> originalGetService(const std::string& name, bool getStub) {
    using GetService = sp<hs::ISensorManager> (*)(const std::string&, bool);
    static const GetService function = [] {
        void* library = dlopen(kHidlLibrary, RTLD_NOW | RTLD_NOLOAD);
        void* symbol = library ? dlsym(library, kGetService) : nullptr;
        if (!symbol) ALOGE("No %s in %s: %s", kGetService, kHidlLibrary, dlerror());
        return reinterpret_cast<GetService>(symbol);
    }();
    return function ? function(name, getStub) : nullptr;
}

}
}

namespace android::frameworks::sensorservice::V1_0 {

::android::sp<ISensorManager> ISensorManager::getService(const std::string& serviceName,
                                                        bool getStub) {
    if (serviceName == "default" && ::android::base::GetBoolProperty(mindone::camera::kSwitch,
                                                                     false)) {
        auto shim = mindone::camera::createSensorManager();
        if (shim) return shim;
    }
    return mindone::camera::originalGetService(serviceName, getStub);
}

}
