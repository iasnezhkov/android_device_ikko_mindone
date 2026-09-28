#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Adapted from a LineageOS device tree for the same MT6789
# chipset) - LINEAGE-PLAN par. 6.3 item 4. Most of this file is MediaTek-platform-generic
# PRODUCT_PACKAGES/PRODUCT_COPY_FILES (audio/BT/sensors/wifi/power HAL service names,
# standard AOSP permission-xml copies) and kept as-is. 🔴 Genuinely device-specific bits
# marked below — NOT fabricated, left as explicit gaps:
#   - carrier-config overlays — these are per-device RRO packages, so none carried over.
#     The framework overlays this device does need are in overlay/, and its key layout is
#     keylayout/mtk-kpd.kl; both are installed below.
#   - fstab.mt6789* → fstab.mt8781* (ro.hardware=mt8781, read from the device —
#     NOT mt6789, even though vendor/etc/fstab.mt6789 also physically exists on our device
#     as an unused vendor fallback; init actually resolves fstab.${ro.hardware}=fstab.mt8781).
#   - PRODUCT_SHIPPING_API_LEVEL 34→35 (this device's stock ships Android 15,
#     ro.build.fingerprint AP3A.240905.015.A2, DEVICE-MAP).

# Installs gsi keys into ramdisk, to boot a developer GSI with verified boot.
$(call inherit-product, $(SRC_TARGET_DIR)/product/developer_gsi_keys.mk)

# Dalvik VM Configuration
$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)

# Project ID Quota
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Enforce generic ramdisk allow list
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_ramdisk.mk)

# A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/launch_with_vendor_ramdisk.mk)

PRODUCT_PACKAGES += \
    com.android.hardware.boot \
    android.hardware.boot-service.default_recovery

PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier

PRODUCT_PACKAGES_DEBUG += \
    update_engine_client

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=ext4 \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=ext4 \
    POSTINSTALL_OPTIONAL_vendor=true

PRODUCT_PACKAGES += \
    checkpoint_gc \
    otapreopt_script

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := hdpi

# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio@7.0-impl \
    android.hardware.audio.effect@7.0-impl \
    android.hardware.audio.service

PRODUCT_PACKAGES += \
    audio.bluetooth.default \
    android.hardware.bluetooth.audio-impl

PRODUCT_PACKAGES += \
    audio.r_submix.default \
    audio.usb.default \
    libaudiofoundation.vendor \
    libbluetooth_audio_session \
    libalsautils \
    libnbaio_mono \
    libtinycompress \
    libdynproc \
    libhapticgenerator

# Speech tuning: the stock handset uplink volume index (23 -> mic PGA 18 dB) clips on loud speech,
# and the uplink DRC block the firmware provides is left switched off. Both are corrected, but the
# tables themselves are MediaTek's and are not carried here -- they are extracted from the device
# like every other proprietary file and tuned in place by audio_param_tuning.py, which
# extract-files.py runs. Live override path for tests: /data/vendor/audiohal/audio_param/
# (the HAL reloads it on restart).

MINDONE_OWN_CODEC2 ?= true

ifneq ($(MINDONE_OWN_CODEC2),true)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/seccomp/android.hardware.media.c2@1.2-extended-seccomp-policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/android.hardware.media.c2@1.2-extended-seccomp-policy
endif

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(LOCAL_PATH)/configs/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(LOCAL_PATH)/configs/audio/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(LOCAL_PATH)/configs/audio/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.mediatek

# Dynamic Partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true
PRODUCT_BUILD_SUPER_PARTITION := true

PRODUCT_PACKAGES += \
    fastbootd

PRODUCT_PACKAGES += \
    FMRadio

# Gatekeeper
# 06.09: + pilot bridge AIDL gatekeeper V1 -> HIDL 1.0 (device/ikko/mindone/bridges/gatekeeper, BRIDGES-HIDL-AIDL)
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-impl \
    android.hardware.gatekeeper@1.0-service \
    android.hardware.gatekeeper-service.mindone \
    android.hardware.biometrics.fingerprint-service.mindone \
    android.hardware.secure_element-service.mindone \
    android.hardware.tetheroffload-service.mindone \
    volte_md_status

MINDONE_CAMERA_PROXY ?= false
ifeq ($(MINDONE_CAMERA_PROXY),true)
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.6-service.mindone
endif

# GMS
ifeq ($(WITH_GMS),true)
GMS_MAKEFILE=gms_minimal.mk
endif

# Graphics
PRODUCT_PACKAGES += \
    android.hardware.graphics.composer@2.3-service

# Health
PRODUCT_PACKAGES += \
    android.hardware.health-service.mediatek \
    android.hardware.health-service.mediatek-recovery

$(call inherit-product, hardware/lineage/compat/frameworks/compat.mk)
$(call inherit-product, hardware/mediatek/frameworks/mediatek-frameworks.mk)

PRODUCT_BOOT_JARS += \
    mediatek-ims-base


# Keylayout - keylayout/mtk-kpd.kl, installed below. The keypad reports through the MediaTek
# kpd driver rather than a generic gpio-keys node, so the standard gpio-keys.kl does not apply.

# Lights
PRODUCT_PACKAGES += \
    android.hardware.light-service.lineage

$(call soong_config_set_bool,lineagelight,snap_rgb_to_pure,true)

# Media
PRODUCT_COPY_FILES += \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_c2_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_c2_video.xml

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/media/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml

PRODUCT_PACKAGES += \
    mediacodec.policy \
    mediaextractor.policy \
    mediaswcodec.policy

ifeq ($(MINDONE_OWN_CODEC2),true)
PRODUCT_PACKAGES += \
    android.hardware.media.c2-service-v4l2 \
    android.hardware.media.c2-v4l2-extended-seccomp_policy

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/media/android.hardware.media.c2@1.2-mediatek-empty.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/android.hardware.media.c2@1.2-mediatek.rc \
    $(LOCAL_PATH)/configs/media/android.hardware.media.c2-v4l2-mindone.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/android.hardware.media.c2-v4l2-mindone.rc \
    $(LOCAL_PATH)/configs/media/media_codecs_mindone_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    $(LOCAL_PATH)/configs/media/media_codecs_performance_mindone_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance_c2.xml

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.v4l2_codec2.decoder.supported.h264=true \
    ro.vendor.v4l2_codec2.decoder.supported.hevc=true \
    ro.vendor.v4l2_codec2.decoder.supported.mpeg4=true \
    ro.vendor.v4l2_codec2.decoder.supported.h263=true \
    ro.vendor.v4l2_codec2.encoder.supported.h264=true \
    ro.vendor.v4l2_codec2.encoder.supported.hevc=true

PRODUCT_VENDOR_PROPERTIES += \
    media.c2.hal.selection=aidl
endif

# Memtrack
PRODUCT_PACKAGES += \
    android.hardware.memtrack-service.mediatek

PRODUCT_PACKAGES += \
    android.hardware.nfc-service.nxp \
    init.nfc.rc

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/nfc/libnfc-nci.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-nci.conf \
    $(LOCAL_PATH)/configs/nfc/libnfc-nxp.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-nxp.conf \
    $(LOCAL_PATH)/configs/nfc/libnfc-nxp_RF.conf:$(TARGET_COPY_OUT_VENDOR)/libnfc-nxp_RF.conf

# Overlays - overlay/ carries this device's own resource overrides (navigation bar geometry and
# handle colour, keyboard bottom padding, SystemUI and lineage-sdk config), applied through
# DEVICE_PACKAGE_OVERLAYS further down. No carrier-config RROs are carried.
# PRODUCT_ENFORCE_RRO_TARGETS is left in place - the framework creates default RROs for
# system/vendor on its own.
$(call inherit-product, hardware/mediatek/overlay/mssi.mk)

# (LineageSDKResCommon no longer exists in lineage-23.2 — Kati reports a non-existent module)

PRODUCT_ENFORCE_RRO_TARGETS := *

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.faketouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.faketouch.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
    frameworks/native/data/etc/android.hardware.nfc.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.uicc.xml \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepdetector.xml \
    frameworks/native/data/etc/android.hardware.se.omapi.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.uicc.xml \
    frameworks/native/data/etc/android.hardware.telephony.cdma.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.cdma.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.ims.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.compute.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-2021-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2021-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level.xml \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

# Platform - ours, PROVEN (ro.board.platform=mt6789, read from the device)
TARGET_BOARD_PLATFORM := mt6789

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/perf/powerhint.json:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.json

PRODUCT_PACKAGES += \
    android.hardware.power-service.lineage-libperfmgr \
    libmtkperf_client_vendor \
    libmtkperf_client \
    vendor.mediatek.hardware.mtkpower@1.2-service.stub

ENABLE_VENDOR_RIL_SERVICE := true

# Rootdir - 🔴 fstab.mt6789* -> fstab.mt8781* (our ro.hardware=mt8781, NOT mt6789)
# init.mt6789.usb.rc comes from hardware/mediatek/aidl/gadget (LineageOS USB gadget HAL).
PRODUCT_PACKAGES += \
    chipinfo \
    fstab.mt8781 \
    fstab.mt8781.vendor_ramdisk \
    init.aee.rc \
    init.cgroup.rc \
    init.connectivity.rc \
    init.connectivity.common.rc \
    init_connectivity.rc \
    init.modem.rc \
    init.mt8781.rc \
    init.mtkgki.rc \
    init.project.rc \
    init.sensor_2_0.rc \
    ueventd.mt6789.rc \
    init.connsys_drv.rc \
    init.mindone_bootlog.rc \
    init.gauge_learned.rc \
    gauge-learned.sh \
    mindone-bootlog.sh \
    mindone-fast-insmod.sh \
    mindone-insmod.schedule

PRODUCT_PACKAGES += \
    init.recovery.mt8781.rc

PRODUCT_PACKAGES += \
    android.hardware.sensors-service.mindone

PRODUCT_PACKAGES += \
    audio.primary.mindone

PRODUCT_COPY_FILES += \
    device/ikko/mindone/audio-hf/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml


PRODUCT_PACKAGES += \
    mindone_mux

PRODUCT_PACKAGES += \
    android.hardware.thermal-service.mediatek

PRODUCT_COPY_FILES += \
    device/ikko/mindone/configs/thermal/thermal_info_config.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json

PRODUCT_PACKAGES += \
    MindOneKeyHandler

PRODUCT_COPY_FILES += \
    device/ikko/mindone/keylayout/mtk-kpd.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/mtk-kpd.kl

# Our own launcher as a privileged system app (device/ikko/mindone/launcher = the launcher project's rom/
# directory + Launcher.apk, system flavor 27b359c). OFF by default: the system flavor claims the HOME
# role (priority 1) and it is deliberately (13.09) not the default yet. Flip to true to ship it;
# then also clear config_mainBuiltInDisplayCutout / set status_bar_height 28dp in overlay/ (README step 7).
WITH_MINDONE_LAUNCHER ?= false
ifeq ($(WITH_MINDONE_LAUNCHER),true)
PRODUCT_PACKAGES += \
    Launcher
DEVICE_PACKAGE_OVERLAYS += device/ikko/mindone/launcher/overlay
endif

PRODUCT_SHIPPING_API_LEVEL := 31
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

PRODUCT_SYSTEM_PROPERTIES += persist.sys.usb.config=adb

PRODUCT_SYSTEM_PROPERTIES += ro.telephony.sim.count=1

PRODUCT_SYSTEM_PROPERTIES += service.sf.prime_shader_cache=1

PRODUCT_SYSTEM_PROPERTIES += dalvik.vm.background-dex2oat-cpu-set=0,1,2,3,4,5
PRODUCT_SYSTEM_PROPERTIES += dalvik.vm.background-dex2oat-threads=3

PRODUCT_HIDL_ENABLED := true
PRODUCT_PACKAGES += \
    hwservicemanager \
    vndservicemanager \
    vndservice \
    android.hidl.allocator@1.0-service \
    android.hidl.memory@1.0-impl

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH) \
    external/v4l2_codec2 \
    hardware/google/interfaces \
    hardware/google/pixel \
    hardware/lineage/interfaces/power-libperfmgr \
    hardware/mediatek \
    hardware/mediatek/libmtkperf_client

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb-service.mediatek \
    android.hardware.usb.gadget-service.mediatek \
    init.mt6789.usb.rc

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.legacy

# Wi-Fi
$(call soong_config_set_bool,mediatek_wifi_hal,use_pre_u_qpr2_struct,true)

PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    hostapd \
    libwifi-hal-wrapper \
    wlan_assistant \
    wpa_supplicant

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/wifi/p2p_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant_overlay.conf \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

# Inherit the proprietary files
$(call inherit-product, vendor/ikko/mindone/mindone-vendor.mk)

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.rc=/vendor/etc/init/hw/ \
    ro.vendor.init.sensor.rc=init.sensor_2_0.rc

PRODUCT_VENDOR_PROPERTIES += \
    persist.vendor.enable.thermal.genl=true

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/privapp-permissions-mediatek-ims.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp-permissions-mediatek-ims.xml \
    $(LOCAL_PATH)/configs/permissions/privapp-permissions-mindone-gms.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/privapp-permissions-mindone-gms.xml \
    $(LOCAL_PATH)/configs/appcompat/compat_framework_overrides.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/appcompat/compat_framework_overrides.xml

# 🔴 The ro.control_privapp_permissions property itself is set NOT here, but in lineage_mindone.mk after
# all the inherits (07.09). Reason: LineageOS sets it to `enforce`
# (vendor/lineage/config/common.mk:108), and a second line with the same key no longer "wins" but
# FAILS the build: `error: found duplicate sysprop assignments`. So the fix is only possible where
# both lines have already been accumulated and the other one can be overridden.

DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Boot animation: LineageOS generates bootanimation.zip sized to the screen (vendor/lineage/config/common.mk:135).
# Without these lines it falls back to the default 1080x1920, but our panel is 1080x1240 (DRM: the only mode
# is 1080x1240@96, dumpsys display: DisplayDeviceInfo 1080 x 1240) - the animation would not match the screen.
TARGET_SCREEN_WIDTH := 1080
TARGET_SCREEN_HEIGHT := 1240

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.fingerprint.xml

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/init/init.mindone-connsys.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.mindone-connsys.rc
