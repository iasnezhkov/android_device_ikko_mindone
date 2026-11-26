/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "CameraDeviceSession.h"
#include "CameraDeviceCallback.h"
#include "HighFps.h"
#include <aidl/android/hardware/camera/device/BnCameraDeviceSession.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android/hardware/camera/device/3.7/ICameraDeviceSession.h>
#include <log/log.h>
#include <algorithm>
#include <map>
namespace mindone::camera {
namespace hc = ::android::hardware::camera::common::V1_0;
using ndk::ScopedAStatus;
using MetadataQueue = ::aidl::android::hardware::common::fmq::MQDescriptor<int8_t,
        ::aidl::android::hardware::common::fmq::SynchronizedReadWrite>;
static ScopedAStatus status(hc::Status value) {
    if (value == hc::Status::OK) return ScopedAStatus::ok();
    int32_t mapped = static_cast<int32_t>(value);
    if (value == hc::Status::METHOD_NOT_SUPPORTED) mapped = 4;
    else if (mapped >= static_cast<int32_t>(hc::Status::OPERATION_NOT_SUPPORTED)) --mapped;
    return ScopedAStatus::fromServiceSpecificError(mapped);
}
static void convertHal(const hd::V3_2::HalStream& s, ad::HalStream* t) {
    t->id = s.id;
    t->overrideFormat = static_cast<decltype(t->overrideFormat)>(s.overrideFormat);
    t->producerUsage = static_cast<decltype(t->producerUsage)>(s.producerUsage);
    t->consumerUsage = static_cast<decltype(t->consumerUsage)>(s.consumerUsage);
    t->maxBuffers = s.maxBuffers;
    t->supportOffline = false;
}
static void convertHal(const hd::V3_3::HalStream& s, ad::HalStream* t) {
    convertHal(s.v3_2, t);
    t->overrideDataSpace = static_cast<decltype(t->overrideDataSpace)>(s.overrideDataSpace);
}
static void convertHal(const hd::V3_4::HalStream& s, ad::HalStream* t) {
    convertHal(s.v3_3, t);
    t->physicalCameraId = s.physicalCameraId.c_str();
}
static void convertHal(const hd::V3_6::HalStream& s, ad::HalStream* t) {
    convertHal(s.v3_4, t);
}
class CameraDeviceSession : public ad::BnCameraDeviceSession {
  public:
    CameraDeviceSession(const ::android::sp<hd::V3_2::ICameraDeviceSession>& backend,
                        const ::android::sp<hd::V3_5::ICameraDeviceCallback>& callback,
                        std::unique_ptr<SensorOwner::Hold> hold, Reopen reopen)
        : mBackend(backend), mCallback(callback), mQueues(getMetadataQueues(callback)),
          mHold(std::move(hold)), mReopen(std::move(reopen)),
          mRetry(mHold && mHold->handover() && mReopen) {}
    ~CameraDeviceSession() override {
        mQueues->oisModes.park();
        if (!mClosed && mBackendOpen) mBackend->close();
    }
    template <class T> bool probe(::android::sp<T>* target) {
        auto cast = T::castFrom(mBackend);
        if (!cast.isOk()) return false;
        *target = cast;
        return true;
    }
    bool probeAll() {
        m33.clear();
        m34.clear();
        m35.clear();
        m36.clear();
        m37.clear();
        return probe(&m33) && probe(&m34) && probe(&m35) && probe(&m36) && probe(&m37);
    }
    bool initialize() {
        return mQueues->request.isValid() && mQueues->result.isValid() && probeAll();
    }
    ScopedAStatus close() override {
        mQueues->oisModes.park();
        bool closed = !mBackendOpen || mBackend->close().isOk();
        if (closed) {
            std::lock_guard<std::mutex> lock(mQueues->resultMutex);
            mQueues->backend.reset();
            mClosed = true;
            mRetry = false;
            mHold.reset();
        }
        return status(closed ? hc::Status::OK : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus configureStreams(const ad::StreamConfiguration& source,
            std::vector<ad::HalStream>* streams) override {
        streams->clear();
        if (!validMetadata(source.sessionParams.metadata)) {
            return status(hc::Status::ILLEGAL_ARGUMENT);
        }
        if (!mBackendOpen) return status(hc::Status::INTERNAL_ERROR);
        const auto start = SensorOwner::Clock::now();
        mHighFpsStreams = highFpsEnabled() && highFpsStreams(source);
        ad::StreamConfiguration adjusted;
        const ad::StreamConfiguration* target = &source;
        if (mHighFpsStreams && highFpsRequested(source.sessionParams.metadata)) {
            adjusted = source;
            if (requestHighFps(&adjusted.sessionParams.metadata)) target = &adjusted;
        }
        bool internal = false;
        auto configured = configureBackend(*target, source.streamConfigCounter, streams, &internal);
        if (internal && mRetry) {
            configured = retryConfigure(*target, source.streamConfigCounter, streams,
                                        start + mHold->timing().retry);
        }
        if (!configured.isOk()) return configured;
        mRetry = false;
        mInputSizes.clear();
        for (const auto& stream : source.streams) {
            if (stream.streamType == ad::StreamType::INPUT) {
                mInputSizes[stream.id] = {stream.width, stream.height};
            }
        }
        std::lock_guard<std::mutex> lock(mQueues->resultMutex);
        auto queue = mBackend->getCaptureResultMetadataQueue([&](const auto& descriptor) {
            mQueues->backend = std::make_unique<HidlQueue>(descriptor);
            if (!mQueues->backend->isValid()) mQueues->backend.reset();
        });
        return status(queue.isOk() ? hc::Status::OK : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus configureBackend(const ad::StreamConfiguration& target, int32_t counter,
                                   std::vector<ad::HalStream>* streams, bool* internal) {
        *internal = false;
        streams->clear();
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto receive = [&](auto code, const auto& config) {
            value = code;
            if (code != hc::Status::OK) return;
            streams->resize(config.streams.size());
            for (size_t i = 0; i < streams->size(); ++i) {
                convertHal(config.streams[i], &(*streams)[i]);
            }
        };
        ::android::hardware::Return<void> result;
        if (m37) {
            hd::V3_7::StreamConfiguration config;
            auto converted = convertStreams(target, &config);
            if (!converted.isOk()) return converted;
            result = m37->configureStreams_3_7(config, receive);
        } else {
            hd::V3_5::StreamConfiguration config;
            auto converted = convertStreams(target, &config.v3_4,
                                            mQueues->pixelModes.enabled());
            if (!converted.isOk()) return converted;
            config.streamConfigCounter = counter;
            if (m36) result = m36->configureStreams_3_6(config, receive);
            else if (m35) result = m35->configureStreams_3_5(config, receive);
            else if (m34) result = m34->configureStreams_3_4(config.v3_4, receive);
            else {
                if (config.v3_4.sessionParams.size() != 0) {
                    return status(hc::Status::ILLEGAL_ARGUMENT);
                }
                hd::V3_2::StreamConfiguration base;
                base.operationMode = config.v3_4.operationMode;
                base.streams.resize(config.v3_4.streams.size());
                for (size_t i = 0; i < base.streams.size(); ++i) {
                    const auto& s = config.v3_4.streams[i];
                    if (!s.physicalCameraId.empty() || s.bufferSize) {
                        return status(hc::Status::ILLEGAL_ARGUMENT);
                    }
                    base.streams[i] = s.v3_2;
                }
                if (m33) result = m33->configureStreams_3_3(base, receive);
                else result = mBackend->configureStreams(base, receive);
            }
        }
        if (!result.isOk() || value != hc::Status::OK) {
            *internal = !result.isOk() || value == hc::Status::INTERNAL_ERROR;
            return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
        }
        return ScopedAStatus::ok();
    }
    void closeBackend() {
        if (!mBackend->close().isOk()) ALOGW("%s: closing the HAL session failed",
                                             mHold->name().c_str());
        mBackendOpen = false;
    }
    ScopedAStatus retryConfigure(const ad::StreamConfiguration& target, int32_t counter,
                                 std::vector<ad::HalStream>* streams,
                                 SensorOwner::Clock::time_point deadline) {
        mRetry = false;
        ALOGW("%s: the HAL failed to configure streams after %s released the shared sensor, "
              "reopening", mHold->name().c_str(), mHold->previous().c_str());
        ScopedAStatus configured = status(hc::Status::INTERNAL_ERROR);
        HandoverSteps steps;
        steps.configure = [&] {
            bool internal = false;
            configured = configureBackend(target, counter, streams, &internal);
            return internal;
        };
        steps.close = [&] { closeBackend(); };
        steps.reopen = [&] {
            auto fresh = mReopen();
            if (!fresh) return false;
            mBackend = fresh;
            mBackendOpen = true;
            if (probeAll()) return true;
            closeBackend();
            return false;
        };
        auto outcome = retryHandover(steps, mHold->timing(), deadline);
        if (!outcome.open) {
            ALOGE("%s: no working HAL session after %d reopens", mHold->name().c_str(),
                  outcome.reopens);
            return status(hc::Status::INTERNAL_ERROR);
        }
        if (!outcome.reopens) return status(hc::Status::INTERNAL_ERROR);
        if (configured.isOk()) {
            ALOGI("%s: streams configured after %d reopens", mHold->name().c_str(),
                  outcome.reopens);
        } else {
            ALOGW("%s: reopen %d: %s", mHold->name().c_str(), outcome.reopens,
                  configured.getDescription().c_str());
        }
        return configured;
    }
    ScopedAStatus constructDefaultRequestSettings(ad::RequestTemplate type,
            ad::CameraMetadata* metadata) override {
        if (!mBackendOpen) return status(hc::Status::INTERNAL_ERROR);
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = mBackend->constructDefaultRequestSettings(
                static_cast<hd::V3_2::RequestTemplate>(type), [&](auto code, const auto& bytes) {
                    value = code;
                    metadata->metadata.assign(bytes.begin(), bytes.end());
                });
        if (value == hc::Status::OK && !validMetadata(metadata->metadata)) {
            value = hc::Status::INTERNAL_ERROR;
        }
        if (result.isOk() && value == hc::Status::OK && mQueues->oisModes.enabled() &&
            !mQueues->oisModes.setDefaultRequest(type, &metadata->metadata)) {
            value = hc::Status::INTERNAL_ERROR;
        }
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus flush() override {
        if (!mBackendOpen) return status(hc::Status::INTERNAL_ERROR);
        auto result = mBackend->flush();
        return status(result.isOk() ? static_cast<hc::Status>(result) : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus getCaptureRequestMetadataQueue(MetadataQueue* queue) override {
        *queue = mQueues->request.dupeDesc();
        return ScopedAStatus::ok();
    }
    ScopedAStatus getCaptureResultMetadataQueue(MetadataQueue* queue) override {
        *queue = mQueues->result.dupeDesc();
        return ScopedAStatus::ok();
    }
    ScopedAStatus isReconfigurationRequired(const ad::CameraMetadata& oldParams,
            const ad::CameraMetadata& newParams, bool* required) override {
        *required = true;
        if (!validMetadata(oldParams.metadata) || !validMetadata(newParams.metadata)) {
            return status(hc::Status::ILLEGAL_ARGUMENT);
        }
        if (mHighFpsStreams && highFpsRequested(oldParams.metadata) !=
                                       highFpsRequested(newParams.metadata)) {
            return ScopedAStatus::ok();
        }
        if (!m35) return ScopedAStatus::ok();
        if (!mBackendOpen) return status(hc::Status::INTERNAL_ERROR);
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = m35->isReconfigurationRequired(oldParams.metadata, newParams.metadata,
                [&](auto code, bool change) { value = code; *required = change; });
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus processCaptureRequest(const std::vector<ad::CaptureRequest>& requests,
            const std::vector<ad::BufferCache>& caches, int32_t* processed) override {
        std::lock_guard<std::mutex> lock(mQueues->requestMutex);
        *processed = 0;
        if (!mBackendOpen) return status(hc::Status::INTERNAL_ERROR);
        ::android::hardware::hidl_vec<hd::V3_7::CaptureRequest> full;
        full.resize(requests.size());
        auto metadata = [&](int64_t size, const auto& bytes, auto* target) {
            if (size < 0 || static_cast<uint64_t>(size) > mQueues->request.availableToRead()) {
                return false;
            }
            if (!size) { *target = bytes; return validMetadata(*target); }
            target->resize(size);
            return mQueues->request.read(reinterpret_cast<int8_t*>(target->data()), size) &&
                    validMetadata(*target);
        };
        auto buffer = [](const ad::StreamBuffer& s, hd::V3_2::StreamBuffer* t) {
            t->streamId = s.streamId;
            t->bufferId = s.bufferId;
            t->status = static_cast<hd::V3_2::BufferStatus>(s.status);
            if (s.acquireFence.fds.size() > 1 || !s.acquireFence.ints.empty()) return false;
            auto handle = [](const auto& source, auto* target) {
                if (::android::isAidlNativeHandleEmpty(source)) return true;
                auto* native = ::android::dupFromAidl(source);
                if (!native) return false;
                target->setTo(native, true);
                for (int i = 0; i < native->numFds; ++i) if (native->data[i] < 0) return false;
                return true;
            };
            return handle(s.buffer, &t->buffer) && handle(s.acquireFence, &t->acquireFence);
        };
        for (size_t i = 0; i < requests.size(); ++i) {
            const auto& s = requests[i];
            auto& t = full[i];
            auto& base = t.v3_4.v3_2;
            if (s.inputWidth < 0 || s.inputHeight < 0 ||
                    (!m34 && !s.physicalCameraSettings.empty())) {
                return status(hc::Status::ILLEGAL_ARGUMENT);
            }
            if (!m37 && s.inputBuffer.streamId != -1) {
                auto dimensions = mInputSizes.find(s.inputBuffer.streamId);
                if (dimensions == mInputSizes.end() ||
                        dimensions->second != std::make_pair(s.inputWidth, s.inputHeight)) {
                    return status(hc::Status::ILLEGAL_ARGUMENT);
                }
            }
            t.inputWidth = s.inputWidth;
            t.inputHeight = s.inputHeight;
            base.frameNumber = s.frameNumber;
            base.fmqSettingsSize = 0;
            if (!metadata(s.fmqSettingsSize, s.settings.metadata, &base.settings) ||
                    !buffer(s.inputBuffer, &base.inputBuffer)) {
                return status(hc::Status::ILLEGAL_ARGUMENT);
            }
            base.outputBuffers.resize(s.outputBuffers.size());
            for (size_t j = 0; j < s.outputBuffers.size(); ++j) {
                if (!buffer(s.outputBuffers[j], &base.outputBuffers[j])) {
                    return status(hc::Status::ILLEGAL_ARGUMENT);
                }
            }
            t.v3_4.physicalCameraSettings.resize(s.physicalCameraSettings.size());
            for (size_t j = 0; j < s.physicalCameraSettings.size(); ++j) {
                const auto& physical = s.physicalCameraSettings[j];
                auto& out = t.v3_4.physicalCameraSettings[j];
                out.physicalCameraId = physical.physicalCameraId;
                out.fmqSettingsSize = 0;
                if (!metadata(physical.fmqSettingsSize, physical.settings.metadata,
                              &out.settings)) {
                    return status(hc::Status::ILLEGAL_ARGUMENT);
                }
            }
        }
        auto& pixelModes = mQueues->pixelModes;
        auto& oisModes = mQueues->oisModes;
        std::vector<uint32_t> frames;
        if (pixelModes.enabled() || oisModes.enabled()) {
            std::vector<std::vector<uint8_t>> settings(full.size());
            for (size_t i = 0; i < full.size(); ++i) {
                const auto& source = full[i].v3_4.v3_2.settings;
                settings[i].assign(source.begin(), source.end());
                frames.push_back(full[i].v3_4.v3_2.frameNumber);
            }
            if (pixelModes.enabled() && !pixelModes.mapRequests(frames, &settings)) {
                return status(hc::Status::ILLEGAL_ARGUMENT);
            }
            if (oisModes.enabled() && !oisModes.mapRequests(frames, &settings)) {
                if (pixelModes.enabled()) pixelModes.keep(frames, 0);
                return status(hc::Status::ILLEGAL_ARGUMENT);
            }
            for (size_t i = 0; i < full.size(); ++i) full[i].v3_4.v3_2.settings = settings[i];
        }
        ::android::hardware::hidl_vec<hd::V3_2::BufferCache> removals;
        removals.resize(caches.size());
        for (size_t i = 0; i < caches.size(); ++i) {
            removals[i] = {caches[i].streamId, static_cast<uint64_t>(caches[i].bufferId)};
        }
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto receive = [&](auto code, uint32_t count) { value = code; *processed = count; };
        ::android::hardware::Return<void> result;
        if (m37) result = m37->processCaptureRequest_3_7(full, removals, receive);
        else if (m34) {
            ::android::hardware::hidl_vec<hd::V3_4::CaptureRequest> converted;
            converted.resize(full.size());
            for (size_t i = 0; i < full.size(); ++i) converted[i] = full[i].v3_4;
            result = m34->processCaptureRequest_3_4(converted, removals, receive);
        } else {
            ::android::hardware::hidl_vec<hd::V3_2::CaptureRequest> converted;
            converted.resize(full.size());
            for (size_t i = 0; i < full.size(); ++i) converted[i] = full[i].v3_4.v3_2;
            result = mBackend->processCaptureRequest(converted, removals, receive);
        }
        if (!frames.empty()) {
            const size_t accepted = result.isOk() && value == hc::Status::OK ?
                    std::min<size_t>(static_cast<size_t>(*processed), frames.size()) : 0;
            if (pixelModes.enabled()) pixelModes.keep(frames, accepted);
            if (oisModes.enabled()) oisModes.keep(frames, accepted);
        }
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus signalStreamFlush(const std::vector<int32_t>& streams, int32_t counter) override {
        if (!m35) return ScopedAStatus::ok();
        if (!mBackendOpen) return status(hc::Status::INTERNAL_ERROR);
        auto result = m35->signalStreamFlush(streams, static_cast<uint32_t>(counter));
        return status(result.isOk() ? hc::Status::OK : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus switchToOffline(const std::vector<int32_t>&, ad::CameraOfflineSessionInfo*,
            std::shared_ptr<ad::ICameraOfflineSession>*) override {
        return status(hc::Status::ILLEGAL_ARGUMENT);
    }
    ScopedAStatus repeatingRequestEnd(int32_t, const std::vector<int32_t>&) override {
        return ScopedAStatus::ok();
    }
  private:
    ::android::sp<hd::V3_2::ICameraDeviceSession> mBackend;
    const ::android::sp<hd::V3_5::ICameraDeviceCallback> mCallback;
    const std::shared_ptr<MetadataQueues> mQueues;
    std::unique_ptr<SensorOwner::Hold> mHold;
    const Reopen mReopen;
    bool mRetry;
    bool mBackendOpen = true;
    bool mClosed = false;
    bool mHighFpsStreams = false;
    std::map<int32_t, std::pair<int32_t, int32_t>> mInputSizes;
    ::android::sp<hd::V3_3::ICameraDeviceSession> m33;
    ::android::sp<hd::V3_4::ICameraDeviceSession> m34;
    ::android::sp<hd::V3_5::ICameraDeviceSession> m35;
    ::android::sp<hd::V3_6::ICameraDeviceSession> m36;
    ::android::sp<hd::V3_7::ICameraDeviceSession> m37;
};
std::shared_ptr<ad::ICameraDeviceSession> createCameraDeviceSession(
        const ::android::sp<hd::V3_2::ICameraDeviceSession>& backend,
        const ::android::sp<hd::V3_5::ICameraDeviceCallback>& callback,
        std::unique_ptr<SensorOwner::Hold> hold, Reopen reopen) {
    auto session = ndk::SharedRefBase::make<CameraDeviceSession>(backend, callback,
                                                                 std::move(hold),
                                                                 std::move(reopen));
    if (session->initialize()) return session;
    session->close();
    return nullptr;
}
}
