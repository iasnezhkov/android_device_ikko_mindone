#include "CameraProviderHidlConn.h"
#include "CameraProviderProxy.h"
#include "Instances.h"

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <unistd.h>

using ::android::sp;
using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::joinRpcThreadpool;
using mindone::camera::CameraProviderHidlConn;
using mindone::camera::CameraProviderProxy;
using mindone::camera::kPublicProviderInstance;

int main() {
    configureRpcThreadpool(6, true);

    CameraProviderHidlConn conn;
    while (!conn.connect()) {
        ALOGW("mindone.camera: waiting for the real camera provider at internal/9");
        sleep(1);
    }

    sp<CameraProviderProxy> proxy = new CameraProviderProxy(&conn);
    ::android::status_t status = proxy->registerAsService(kPublicProviderInstance);
    if (status != ::android::OK) {
        ALOGE("mindone.camera: registerAsService(%s) failed: %d", kPublicProviderInstance, status);
        return 1;
    }

    ALOGI("mindone.camera: proxy provider is up at %s, forwarding to internal/9", kPublicProviderInstance);
    joinRpcThreadpool();
    return 0;
}
