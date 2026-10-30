/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "LiveDisplayMindone"

#include "mediatek_drm_uapi.h"

#include <aidl/vendor/lineage/livedisplay/BnDisplayColorCalibration.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <xf86drm.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/netlink.h>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <system_error>
#include <thread>
#include <vector>

namespace mindone {

constexpr int32_t kUnity = 2048;
constexpr char kProperty[] = "persist.vendor.livedisplay.rgb";

class DisplayColorCalibration final
    : public aidl::vendor::lineage::livedisplay::BnDisplayColorCalibration {
  public:
    DisplayColorCalibration() {
        const std::string value = android::base::GetProperty(kProperty, "2048 2048 2048");
        std::istringstream input(value);
        std::array<int32_t, 3> saved{};
        std::string extra;
        if ((input >> saved[0] >> saved[1] >> saved[2]) && !(input >> extra) &&
            valid(saved)) {
            rgb_ = saved;
        } else {
            LOG(ERROR) << "Invalid saved RGB; using unity";
        }
    }

    bool initialize() {
        events_.reset(socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT));
        if (events_ < 0) {
            PLOG(ERROR) << "Cannot open display event socket";
            return false;
        }
        const int receiveBuffer = 64 * 1024;
        const int passCredentials = 1;
        if (setsockopt(events_.get(), SOL_SOCKET, SO_RCVBUF, &receiveBuffer,
                       sizeof(receiveBuffer)) < 0 ||
            setsockopt(events_.get(), SOL_SOCKET, SO_PASSCRED, &passCredentials,
                       sizeof(passCredentials)) < 0) {
            PLOG(ERROR) << "Cannot configure display event socket";
            return false;
        }
        sockaddr_nl address{};
        address.nl_family = AF_NETLINK;
        address.nl_groups = 1;
        if (bind(events_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
            PLOG(ERROR) << "Cannot subscribe to display events";
            return false;
        }
        return true;
    }

    void connect() {
        android::base::WaitForProperty("sys.boot_completed", "1");
        android::base::unique_fd fd(open("/dev/dri/card0", O_RDWR | O_CLOEXEC));
        if (fd < 0) {
            PLOG(FATAL) << "Cannot open DRM device";
            return;
        }
        if (drmDropMaster(fd.get()) == 0) {
            LOG(ERROR) << "Opened the DRM device as master after boot completed; dropped it";
        }
        drmVersionPtr version = drmGetVersion(fd.get());
        const bool mediatek = version && version->name &&
                std::string(version->name, version->name_len) == "mediatek";
        if (version) drmFreeVersion(version);
        mtk_drm_pq_caps_info caps{};
        if (!mediatek || ioctl(fd.get(), kGetPqCaps, &caps) < 0 ||
            caps.ccorr_caps.ccorr_bit != 13 || caps.ccorr_caps.ccorr_number != 1 ||
            caps.ccorr_caps.ccorr_linear != 1) {
            LOG(ERROR) << "DRM device does not expose the expected 13-bit linear CCORR";
            std::lock_guard<std::mutex> lock(mutex_);
            unsupported_ = true;
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        fd_ = std::move(fd);
        apply();
    }

    ndk::ScopedAStatus getMaxValue(int32_t* result) override {
        *result = kUnity;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getMinValue(int32_t* result) override {
        *result = 0;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getCalibration(std::vector<int32_t>* result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        result->assign(rgb_.begin(), rgb_.end());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setCalibration(const std::vector<int32_t>& rgb) override {
        if (rgb.size() != 3) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        const std::array<int32_t, 3> next{rgb[0], rgb[1], rgb[2]};
        if (!valid(next)) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string value = std::to_string(next[0]) + " " + std::to_string(next[1]) +
                " " + std::to_string(next[2]);
        if (!android::base::SetProperty(kProperty, value)) {
            return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EIO, "Cannot persist RGB");
        }
        rgb_ = next;
        const int error = apply();
        if (error != 0 && error != EAGAIN) {
            return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                    error, "CCORR apply failed; RGB retained for next display activation");
        }
        return ndk::ScopedAStatus::ok();
    }

    void run() {
        for (;;) {
            std::array<char, 4096> data{};
            alignas(cmsghdr) char control[CMSG_SPACE(sizeof(ucred))]{};
            sockaddr_nl sender{};
            iovec buffer{data.data(), data.size()};
            msghdr message{};
            message.msg_name = &sender;
            message.msg_namelen = sizeof(sender);
            message.msg_iov = &buffer;
            message.msg_iovlen = 1;
            message.msg_control = control;
            message.msg_controllen = sizeof(control);
            const ssize_t count = recvmsg(events_.get(), &message, 0);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
                LOG(FATAL) << "Display event stream failed; restarting for RGB resynchronization";
                return;
            }
            if (message.msg_namelen != sizeof(sender) || sender.nl_family != AF_NETLINK ||
                sender.nl_pid != 0) continue;
            bool trusted = false;
            for (cmsghdr* header = CMSG_FIRSTHDR(&message); header;
                 header = CMSG_NXTHDR(&message, header)) {
                if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_CREDENTIALS &&
                    header->cmsg_len == CMSG_LEN(sizeof(ucred))) {
                    ucred credentials{};
                    std::memcpy(&credentials, CMSG_DATA(header), sizeof(credentials));
                    trusted = credentials.pid == 0 && credentials.uid == 0;
                }
            }
            if (!trusted) continue;
            uint64_t sequence = 0;
            if (!activation(data.data(), static_cast<size_t>(count), &sequence)) continue;
            std::lock_guard<std::mutex> lock(mutex_);
            if (sequence <= lastActivation_) continue;
            lastActivation_ = sequence;
            apply();
        }
    }

  private:
    static bool valid(const std::array<int32_t, 3>& rgb) {
        return std::all_of(rgb.begin(), rgb.end(), [](int32_t v) { return v >= 0 && v <= kUnity; });
    }

    static bool activation(const char* data, size_t size, uint64_t* sequence) {
        bool change = false;
        bool drm = false;
        bool primary = false;
        bool active = false;
        bool numbered = false;
        size_t offset = 0;
        while (offset < size) {
            const void* terminator = std::memchr(data + offset, '\0', size - offset);
            if (!terminator) return false;
            const auto* end = static_cast<const char*>(terminator);
            const std::string field(data + offset, end);
            if (field == "ACTION=change") change = true;
            if (field == "SUBSYSTEM=drm") drm = true;
            if (field == "DEVNAME=dri/card0") primary = true;
            if (field == "MINDONE_DISPLAY_ACTIVE=1") active = true;
            constexpr char prefix[] = "SEQNUM=";
            if (field.compare(0, sizeof(prefix) - 1, prefix) == 0) {
                const auto result = std::from_chars(data + offset + sizeof(prefix) - 1,
                                                    end, *sequence);
                numbered = result.ec == std::errc{} && result.ptr == end && *sequence != 0;
            }
            offset = static_cast<size_t>(end - data) + 1;
        }
        return change && drm && primary && active && numbered;
    }

    int apply() {
        if (unsupported_) return ENODEV;
        if (fd_ < 0) return EAGAIN;
        DRM_DISP_CCORR_COEF_T coefficients{};
        coefficients.hw_id = DRM_DISP_CCORR0;
        for (size_t i = 0; i < rgb_.size(); ++i) coefficients.coef[i][i] = rgb_[i];
        if (ioctl(fd_.get(), kSetCcorr, &coefficients) == 0) return 0;
        const int error = errno;
        if (error != EAGAIN) LOG(ERROR) << "CCORR ioctl failed: " << std::strerror(error);
        return error;
    }

    android::base::unique_fd fd_;
    android::base::unique_fd events_;
    std::mutex mutex_;
    std::array<int32_t, 3> rgb_{kUnity, kUnity, kUnity};
    uint64_t lastActivation_ = 0;
    bool unsupported_ = false;
};

}

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto service = ndk::SharedRefBase::make<mindone::DisplayColorCalibration>();
    if (!service->initialize()) return EXIT_FAILURE;
    const std::string instance = std::string(service->descriptor) + "/default";
    if (AServiceManager_addService(service->asBinder().get(), instance.c_str()) != STATUS_OK) {
        LOG(ERROR) << "Cannot register " << instance;
        return EXIT_FAILURE;
    }
    std::thread([service] { service->connect(); }).detach();
    std::thread([service] { service->run(); }).detach();
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
