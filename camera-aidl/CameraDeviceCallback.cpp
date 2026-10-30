/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraDeviceCallback.h"
#include <hidl/Status.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <log/log.h>
namespace mindone::camera {
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
class CameraDeviceCallback : public hd::V3_5::ICameraDeviceCallback {
  public:
    explicit CameraDeviceCallback(std::shared_ptr<ad::ICameraDeviceCallback> callback)
        : mCallback(std::move(callback)) {}
    Return<void> notify(const hidl_vec<hd::V3_2::NotifyMsg>& messages) override {
        std::vector<ad::NotifyMsg> converted;
        for (const auto& message : messages) {
            if (message.type == hd::V3_2::MsgType::SHUTTER) {
                ad::ShutterMsg shutter;
                shutter.frameNumber = static_cast<int32_t>(message.msg.shutter.frameNumber);
                shutter.timestamp = static_cast<int64_t>(message.msg.shutter.timestamp);
                shutter.readoutTimestamp = 0;
                converted.push_back(ad::NotifyMsg::make<ad::NotifyMsg::Tag::shutter>(shutter));
            } else if (message.type == hd::V3_2::MsgType::ERROR) {
                ad::ErrorMsg error;
                error.frameNumber = static_cast<int32_t>(message.msg.error.frameNumber);
                error.errorStreamId = message.msg.error.errorStreamId;
                error.errorCode = static_cast<ad::ErrorCode>(message.msg.error.errorCode);
                converted.push_back(ad::NotifyMsg::make<ad::NotifyMsg::Tag::error>(error));
            } else {
                return failure();
            }
        }
        auto result = mCallback->notify(converted);
        return result.isOk() ? Return<void>(Void()) : failure();
    }
    Return<void> processCaptureResult(const hidl_vec<hd::V3_2::CaptureResult>& results) override {
        hidl_vec<hd::V3_4::CaptureResult> full;
        full.resize(results.size());
        for (size_t i = 0; i < results.size(); ++i) full[i].v3_2 = results[i];
        return processCaptureResult_3_4(full);
    }
    Return<void> processCaptureResult_3_4(
            const hidl_vec<hd::V3_4::CaptureResult>& results) override {
        std::lock_guard<std::mutex> lock(queues->resultMutex);
        std::vector<ad::CaptureResult> converted(results.size());
        auto metadata = [&](uint64_t size, const auto& bytes, auto* target, int64_t* fmqSize) {
            if (size) {
                if (!queues->backend || size > queues->backend->availableToRead()) return false;
                target->metadata.resize(size);
                if (!queues->backend->read(target->metadata.data(), size)) return false;
            } else target->metadata.assign(bytes.begin(), bytes.end());
            if (!validMetadata(target->metadata)) return false;
            *fmqSize = 0;
            if (!target->metadata.empty() && queues->result.write(
                    reinterpret_cast<const int8_t*>(target->metadata.data()),
                    target->metadata.size())) {
                *fmqSize = target->metadata.size();
                target->metadata.clear();
            }
            return true;
        };
        auto buffer = [](const auto& source, ad::StreamBuffer* target) {
            target->streamId = source.streamId;
            target->bufferId = source.bufferId;
            target->status = static_cast<ad::BufferStatus>(source.status);
            const native_handle_t* fence = source.releaseFence.getNativeHandle();
            if (fence) {
                if (fence->numFds > 1 || fence->numInts) return false;
                target->releaseFence = ::android::dupToAidl(fence);
                for (const auto& fd : target->releaseFence.fds) if (fd.get() < 0) return false;
            }
            return true;
        };
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& s = results[i].v3_2;
            auto& t = converted[i];
            t.frameNumber = s.frameNumber;
            t.partialResult = s.partialResult;
            if (!metadata(s.fmqResultSize, s.result, &t.result, &t.fmqResultSize) ||
                    !buffer(s.inputBuffer, &t.inputBuffer)) return failure();
            t.outputBuffers.resize(s.outputBuffers.size());
            for (size_t j = 0; j < s.outputBuffers.size(); ++j) {
                if (!buffer(s.outputBuffers[j], &t.outputBuffers[j])) return failure();
            }
            t.physicalCameraMetadata.resize(results[i].physicalCameraMetadata.size());
            for (size_t j = 0; j < t.physicalCameraMetadata.size(); ++j) {
                const auto& physical = results[i].physicalCameraMetadata[j];
                auto& out = t.physicalCameraMetadata[j];
                out.physicalCameraId = physical.physicalCameraId.c_str();
                if (!metadata(physical.fmqMetadataSize, physical.metadata,
                              &out.metadata, &out.fmqMetadataSize)) return failure();
            }
        }
        return mCallback->processCaptureResult(converted).isOk() ? Return<void>(Void()) : failure();
    }
    const std::shared_ptr<MetadataQueues> queues = std::make_shared<MetadataQueues>();
    Return<void> requestStreamBuffers(const hidl_vec<hd::V3_5::BufferRequest>&,
                                     requestStreamBuffers_cb callback) override {
        callback(hd::V3_5::BufferRequestStatus::FAILED_UNKNOWN, {});
        return Void();
    }
    Return<void> returnStreamBuffers(const hidl_vec<hd::V3_2::StreamBuffer>&) override {
        return failure();
    }
  private:
    static Return<void> failure() {
        ALOGE("Camera callback conversion unavailable or delivery failed");
        return ::android::hardware::Status::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    const std::shared_ptr<ad::ICameraDeviceCallback> mCallback;
};
::android::sp<hd::V3_5::ICameraDeviceCallback> createCameraDeviceCallback(
        const std::shared_ptr<ad::ICameraDeviceCallback>& callback) {
    return new CameraDeviceCallback(callback);
}
std::shared_ptr<MetadataQueues> getMetadataQueues(
        const ::android::sp<hd::V3_5::ICameraDeviceCallback>& callback) {
    return static_cast<CameraDeviceCallback*>(callback.get())->queues;
}
}
