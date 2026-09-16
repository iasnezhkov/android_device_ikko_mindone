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

# seccomp: the stock codec2 HAL policy (A15) lacks syscalls that A16 bionic/libbinder use → SIGSYS at start (F3817).
# Our extended policy = stock + A16 mediaswcodec/crash_dump names; the stock -mediatek- policy stays proprietary.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/seccomp/android.hardware.media.c2@1.2-extended-seccomp-policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/android.hardware.media.c2@1.2-extended-seccomp-policy

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(LOCAL_PATH)/configs/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(LOCAL_PATH)/configs/audio/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(LOCAL_PATH)/configs/audio/usb_audio_accessory_only_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_accessory_only_policy_configuration.xml

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/a2dp_in_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_in_audio_policy_configuration.xml \
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

# FM Radio — chip confirmed same family (F3308, ro.fm.chip=0x6631 in product.prop)
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
    android.hardware.tetheroffload-service.mindone

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

# IMS — modem/VoLTE not stable on our kernel 6.1 yet (F2574), inherited as placeholder
$(call inherit-product, hardware/lineage/compat/frameworks/compat.mk)
$(call inherit-product, hardware/mediatek/frameworks/mediatek-frameworks.mk)

PRODUCT_BOOT_JARS += \
    mediatek-ims-base

# 🔴 OPEN (01.09): privapp-permissions-com.mediatek.ims.xml was NOT found in our vendor_a.img
# (searched via direct mount+find) - a file of this kind is a permission declaration for ImsService
# as a system_ext priv-app under the LineageOS framework, not an extraction of a stock blob.
# Not fabricated - the line below is COMMENTED OUT, rewrite it when the IMS track is active again (F2574).
#PRODUCT_COPY_FILES += \
#    $(LOCAL_PATH)/configs/privapp-permissions-com.mediatek.ims.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp-permissions-com.mediatek.ims.xml

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

# (the project issue registry, see also F3174/F3250/F3270/F3369 in the fact log): the stock
# media_profiles_V1_0.xml is capped at 1080p, even though the hardware encoder c2.mtk.avc.encoder
# honestly advertises a limit of 2560x1440 (media_codecs_c2.xml), and hardware 2560x1080 encoding
# has already been proven to work (F3270, screenrecord). The file below adds ONLY quality="qhd"
# (2560x1440, the only resolution above 1080p reachable by the camera app - F3369,
# disassembly of SXSCamera showed that "high" never hits this path) for cameraId 0 and 1 (F182: exactly
# 2 logical cameras), and raises VideoEncoderCap h264 maxFrameWidth/Height to 2560x1440;
# the candidate is modules/magisk-2k-recording/vendor/etc/media_profiles_V1_0.xml (already built as a
# systemless overlay for live testing, status "effect NOT VERIFIED on the device"); a byte-for-byte
# identical copy is placed here. 🔴 The line below overrides the file that otherwise comes from
# vendor/ikko/mindone/mindone-vendor.mk (generated by extract-files.py from proprietary-files.txt,
# which explicitly lists the stock vendor/etc/media_profiles_V1_0.xml, line ~274) - that is why
# this line in proprietary-files.txt is COMMENTED OUT (see the comment there); otherwise Make would raise
# "overriding commands for target" for two different sources of the same path - the same trap
# already documented for libcodec2_* (F3827/F3830) and biometrics HIDL (F3831) in this file.
# CHECK ON THE NEXT BUILD: (1) the build does not fail on this file; (2) in the built vendor.img
# `unzip -p .../vendor.img ... | grep -c 'quality="qhd"'`, or a live adb check on the device after flashing,
# gives 2 (was 0).
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/media/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml

PRODUCT_PACKAGES += \
    android.hardware.media.omx@1.0-service \
    mediacodec.policy \
    mediaextractor.policy \
    mediaswcodec.policy

# Memtrack
PRODUCT_PACKAGES += \
    android.hardware.memtrack-service.mediatek

# NFC — the controller is NXP pn553 (F3218, vendor.nxp.nxpnfc in manifest.xml), not an ST part;
# the AIDL HAL from hardware/nxp/nfc. OS-side bring-up is the separate NFC track (needs the
# libnfc-nxp.conf/RF config from the stock vendor, HANDOFF-NFC-3108).
PRODUCT_PACKAGES += \
    android.hardware.nfc-service.nxp

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

# Power - 🔴 OPEN (01.09): powerhint.json was NOT found in our vendor_a.img (searched via direct
# mount+find) - our stock manifest (F3410) carries `vendor.mediatek.hardware.mtkpower`
# (a proprietary MTK HAL), and NOT generic android.hardware.power at all. On MediaTek devices
# `power-service.lineage-libperfmgr` is commonly layered on top of that same mtkpower as a thin AIDL
# shim, but there is NO powerhint.json tuning of our own for this shim, and tuning numbers taken from
# another device describe someone else's hardware. The line is commented out.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/perf/powerhint.json:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.json

PRODUCT_PACKAGES += \
    android.hardware.power-service.lineage-libperfmgr \
    libmtkperf_client_vendor \
    libmtkperf_client \
    vendor.mediatek.hardware.mtkpower@1.2-service.stub

# RIL - modem/VoLTE is not stable on 6.1 (F2574), not a blocker for the first bootable image
ENABLE_VENDOR_RIL_SERVICE := true

# Rootdir - 🔴 fstab.mt6789* -> fstab.mt8781* (our ro.hardware=mt8781, NOT mt6789)
# rootdir/etc = the stock vendor's own init/hw files (init.mt8781.rc is what ro.hardware selects;
# it imports init.connectivity/usb/project/aee/cgroup/modem/sensor rc) + ueventd.rc; the NFC rc
# is NXP (init.SNxxx.nfc.rc) and comes with the blobs, not an ST init.stnfc.rc;
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
    init.mt6789.rc \
    init.mt8781.rc \
    init.mtkgki.rc \
    init.project.rc \
    init.sensor_2_0.rc \
    ueventd.mt6789.rc \
    init.mindone_bootlog.rc \
    mindone-bootlog.sh

PRODUCT_PACKAGES += \
    init.recovery.mt8781.rc

# Sensors: our own AIDL HAL over /dev/hf_manager (device/ikko/mindone/sensors-hf, 13.09).
# It waits for HF_MANAGER_REQUEST_READY_STATUS before answering getSensorsList(), which is the
# fix for the boot race where the stock multihal + sensors.mt6789.so blob answered SensorService
# with an empty list ~10 s before the SCP sensor stack was ready (F4290/F4292). The stock chain
# (android.hardware.sensors-service.multihal + android.hardware.sensors@2.0-subhal-impl-1.0 +
# the 7 vendor blobs) is no longer built or copied.
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.mindone

# 🔴 15.09: our own audio HAL replaces the MediaTek blob. This is not a preference - the stock
# 64-bit blob corrupts its own stack and cannot be used at all (F4464), and the 32-bit one is the
# only reason audio worked before. audio.primary.mindone is source we control, builds for both
# ABIs, and now covers everything this board can do: media, capture, voice calls over the modem
# "PCM 2" crossbar, BT SCO through btcvsd, audio patches, and hardware volume.
# ro.hardware.audio.primary (vendor.prop) is what actually makes libhardware pick it: without that
# the loader walks ro.board.platform=mt6789 and lands on a stock blob or, in 64-bit, on the
# audio.primary.default stub that silently swallows every write (F4461).
PRODUCT_PACKAGES += \
    audio.primary.mindone

PRODUCT_COPY_FILES += \
    device/ikko/mindone/audio-hf/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml

# 🔴 15.09 (F4442 resolved): the open modem userspace is now BUILT. It used to be excluded entirely -
# Android.bp files renamed .disabled - because the ccci daemons linked the stock libnvram/libsysenv,
# which live in the vendor/ikko/mindone soong_namespace and are undefined from the device namespace,
# and importing it would have formed a cycle (vendor already imports device).
# Resolved by binding those five entry points at RUNTIME (modem/ccci-userspace/common/nvram_shim.c):
# dlopen() resolves through the vendor namespace's own search path on the device, so the same stock
# libraries still do the work, but the build graph no longer needs to see them.
#
# Building them is NOT switching to them. None of these has an init_rc, so nothing starts at boot:
# they are inert binaries in the image, started by hand for the A/B against the stock stack
# (tools/rigs/modem-ab.sh). The stock mtkfusionrild/gsm0710muxd keep serving the phone until an A/B
# says otherwise. Having them in the image is what makes that A/B a 34-second edit-build-push loop
# (tools/scripts/fastbuild.sh) instead of a standalone build.
# 15.09: NOT packaged. The decision for the trip build is "stock modem, no ambiguity", and
# shipping our daemons alongside it leaves exactly the ambiguity that call was meant to remove --
# android.hardware.radio@1.6-service.mindone in particular installs an init service (only a
# `disabled` keyword keeps it from starting) and its RIL is a 27-of-201-method skeleton whose
# emergencyDial is still a stub.
#
# Nothing is lost for development: tools/rigs/modem-ab.sh pushes these binaries from $ARTIFACTS
# and swaps one component at a time against the live stock stack, which is how they are exercised
# anyway. Build them with:
#     bash tools/scripts/lineage-m.sh mindone_mdinit mindone_fsd mindone_rpcd mindone_mux \
#          android.hardware.radio@1.6-service.mindone
# Re-enable here only when the RIL is fit to be a daily driver.
#
# PRODUCT_PACKAGES += \
#     mindone_mdinit \
#     mindone_fsd \
#     mindone_rpcd \
#     mindone_mux \
#     android.hardware.radio@1.6-service.mindone

# Thermal: the open LineageOS AIDL HAL (hardware/mediatek/aidl/thermal, Pixel-style, IThermal V3 via its own
# vintf fragment) replaces the stock thermal@2.0-service.mtk blob chain (13.09, THERMAL-HAL-PLAN-1309).
# v1 config is observation only: CPU/GPU throttling is done in-kernel by mindone_thermal.ko (F4268), so no
# BindedCdevInfo targets cpufreq-cpu0/cpufreq-cpu6/mindone-gpufreq — the HAL reports severity that mirrors
# the kernel's 85/80/85 C trips, plus battery/soc/modem/PA/skin(ap_ntc) ladders and emergency shutdown.
PRODUCT_PACKAGES += \
    android.hardware.thermal-service.mediatek

PRODUCT_COPY_FILES += \
    device/ikko/mindone/configs/thermal/thermal_info_config.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json

# Camera flip module (/ F4296): the hall switch reaches userspace as KEY_F2/KEY_F3 on the mtk-kpd
# input device; MindOneKeyHandler (device/ikko/mindone/keyhandler, registered through the lineage-sdk
# overlay config_deviceKeyHandler*) records the module position in Settings.System
# "mindone_camera_flipped" and broadcasts org.mindone.intent.action.CAMERA_FLIPPED; Aperture follows it
# (patches/packages_apps_Aperture-0001-mindone-camera-flip.patch, applied in the build tree).
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

# Shipping API level = the vendor's LAUNCH level, not the current Android version: the stock vendor carries
# ro.board.first_api_level=31 and ro.vndk.version=31 (the stock vendor/build.prop, F3830). At 35 the build
# does not include the apex VNDK 31 and drops hwservicemanager - the stock HIDL/media libraries link against
# A16 versions instead (c2 HAL: SIGSEGV/CANNOT LINK, F3824/F3827). 06.09: 35 -> 31.
PRODUCT_SHIPPING_API_LEVEL := 31
# 13.09 (ROM #1 failed at check_vintf: "No kernel entry found for kernel version 6.12" - the framework
# compatibility matrices in this tree list no 6.12 kernel at any FCM level). The mismatch is proven not to
# block boot (F3082 for 6.1, F4212 for 6.12 + Android 16), so do not let the packaging step enforce it.
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false
# 🔴 08.09 (F3923): VNDK 31 REMOVED - it was unnecessary and harmful.
# With ro.vndk.version=31, linkerconfig builds namespaces from the Android 12-era library set, in
# which libapexsupport.so did NOT YET EXIST. The vendor's libbinder.so (built for A16) requires it -
# and fails to link. As a result vndservicemanager, the screen compositor, camerahalserver,
# audio, codec2, and rild all fail, and because of libnativeloader.so - app_process itself, i.e. zygote.
# Measured: 85 CANNOT LINK errors and 12 zygote restarts in 27s of boot => BLACK SCREEN.
# Proof it is unnecessary: ROM #10 (slot _a, built 05.09 BEFORE this fix) has ro.vndk.version
# EMPTY, and hwservicemanager, vndservicemanager and codec2-HAL are all running, boot
# completes, CANNOT LINK = 0. So everything VNDK was introduced for works without it:
# hwservicemanager is kept alive by PRODUCT_HIDL_ENABLED and an explicit entry in PRODUCT_PACKAGES below,
# not by the API level.
# PRODUCT_EXTRA_VNDK_VERSIONS := 31
# PRODUCT_VENDOR_PROPERTIES += ro.vndk.version=31

# 🔴 05.09 (F3758): the stock vendor is HIDL (composer@2.3, gatekeeper@1.0, allocator/mapper@4.0,
# sensors 2.0, audio 7.0, ...). At SHIPPING_API_LEVEL 35 the build drops hwservicemanager
# (base_system_ext.mk: PRODUCT_PACKAGES_SHIPPING_API_LEVEL_34) and vndservicemanager (base_vendor.mk:
# ..._29): init logs "service hwservicemanager not found", not a single HIDL HAL registers,
# keystore2/vold hang, /data does not mount, the screen is black. The standard path for such devices is
# build/make/core/product.mk:329 (PRODUCT_HIDL_ENABLED + an explicit hwservicemanager in PRODUCT_PACKAGES).
# USB adb on by default (userdebug, ro.adb.secure unset → no key prompt): the only hands-free success
# signal for a bring-up boot is adb appearing once boot completes (F3758: screen may stay dark).
PRODUCT_SYSTEM_PROPERTIES += persist.sys.usb.config=adb

# 12.09 (F4136): ro.telephony.sim.count is marked by AOSP as default_prop (platform
# context) - it must be set by init, not vendor_init. The line used to live in vendor.prop
# and produced avc: denied { set } scontext=vendor_init tcontext=default_prop tclass=property_service
# (device/logs/catch-0911-185910/dmesg.txt.gz); an allow rule cannot fix this (a platform neverallow,
# see sepolicy/vendor/vendor_init.te, F3908). Moved here - it is computed into build.prop, set by
# init, there should be no denial. Check: `getprop ro.telephony.sim.count` = 1 after the build,
# and no avc with tcontext=default_prop tclass=property_service scontext=vendor_init in a fresh dmesg.
PRODUCT_SYSTEM_PROPERTIES += ro.telephony.sim.count=1

# 12.09 (F4226): on startup SurfaceFlinger precompiles Skia shaders on the RenderEngine thread
# ("Shader cache generated 140 shaders in 14581 ms" on the Mali-G57 MC2), and SystemUI calls
# SurfaceControl.getGPUContextPriority in onCreate -> RenderEngineThreaded::getContextPriority queues
# BEHIND primeCache (frameworks/native/libs/renderengine/threaded/RenderEngineThreaded.cpp:320) and waits
# the full 14+ s -> "Process com.android.systemui failed to complete startup" (an ANR on every boot,
# visible ever since the screen has worked from boot). The property is read by SurfaceFlinger::init()
# (services/surfaceflinger/SurfaceFlinger.cpp:1070); with 0, shaders compile on first
# use (a one-time hitch per combination), SF responds immediately. Verified live on 12.09:
# after setprop + restarting SF there is no "Shader cache generated" line, the screen and SystemUI are alive.
# 16.09 (F4507): turning priming off did not make the cost go away, it moved it into
# composition. Traced through five Settings navigations: SurfaceFlinger compiles shaders while
# composing -- 66 ms and 80 ms, each one inside `composite`, each one a cache_miss whose bulk is
# driver_link_program. While that runs no buffer is released to anyone: Settings, SystemUI and
# the splash screen all stall together. The shader being compiled is FillRRectOp, a rounded
# rectangle -- an everyday piece of UI, not something exotic.
#
# It repeats every boot rather than once because the persistent cache is never written:
# /data/misc/surfaceflinger/ stays empty. That cache is gated by the shader_disk_cache aconfig
# flag, which this build has as READ_ONLY=false, and turning it on means editing the release
# configuration in build/, outside this tree -- not something a person building from this
# repository could reproduce.
#
# So priming comes back, but only for the layer kinds this device actually composes. The
# combinations dropped below are what made the original prime take 14 581 ms and ANR SystemUI on
# every boot (F4226): each "dimmed" variant doubles the set, and shadow/PIP layers are not used
# here. Success is measured at boot: no "failed to complete startup" for SystemUI, and no
# shader_compile inside SurfaceFlinger during navigation.
# The switch itself lives here, where its predecessor did. Which layer kinds get primed is set
# in vendor.prop next to the other debug.sf.* values, because that is the file whose debug.sf.*
# entries are known to reach SurfaceFlinger on this device -- the composition durations set
# there were read back from the running system and matched.
PRODUCT_SYSTEM_PROPERTIES += service.sf.prime_shader_cache=1
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

# F3802: init.mt8781.rc imports vendor rc through ${ro.vendor.rc}; stock sets these in vendor build.prop
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.rc=/vendor/etc/init/hw/ \
    ro.vendor.init.sensor.rc=init.sensor_2_0.rc

# F3807: allowlist for stock MTK ImsService priv-app; enforcement relaxed to log for other stock priv-apps
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/privapp-permissions-mediatek-ims.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp-permissions-mediatek-ims.xml \
    $(LOCAL_PATH)/configs/permissions/privapp-permissions-mindone-gms.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/privapp-permissions-mindone-gms.xml
# 🔴 09.09 (F4015): PRODUCT specifically, not SYSTEM_EXT. Privilege lists are matched BY PARTITION
# (AppIdPermissionPolicy.kt, which excludes when), and GmsCore.apk and Phonesky.apk live in
# /product/priv-app. The file in /system_ext was simply being ignored: measured that GMS did not get
# WRITE_DEVICE_CONFIG (absent from install permissions, out of 313 granted), and the log
# accumulated 334 lines of Permission denial to mutate flag per boot.
# Do NOT touch the mediatek-ims line above: ImsService lives in /system_ext, that one is correct.

# 🔴 The ro.control_privapp_permissions property itself is set NOT here, but in lineage_mindone.mk after
# all the inherits (07.09). Reason: LineageOS sets it to `enforce`
# (vendor/lineage/config/common.mk:108), and a second line with the same key no longer "wins" but
# FAILS the build: `error: found duplicate sysprop assignments`. So the fix is only possible where
# both lines have already been accumulated and the other one can be overridden.

# Overlays (F3837: 96 Hz panel as the default refresh rate; sync overlay/ into the build tree ONLY between builds, F3834)
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Boot animation: LineageOS generates bootanimation.zip sized to the screen (vendor/lineage/config/common.mk:135).
# Without these lines it falls back to the default 1080x1920, but our panel is 1080x1240 (DRM: the only mode
# is 1080x1240@96, dumpsys display: DisplayDeviceInfo 1080 x 1240) - the animation would not match the screen.
TARGET_SCREEN_WIDTH := 1080
TARGET_SCREEN_HEIGHT := 1240

# 🔴 09.09 (F4007). Without THIS file the framework does not see the fingerprint sensor at all: `pm list features`
# does not show android.hardware.fingerprint, and `dumpsys biometric` prints an empty Sensors
# list. The kernel, HIDL service and AIDL bridge all work fine - the chain was broken at the last step.
# The stock system declares the feature the same way: /vendor/etc/permissions/android.hardware.fingerprint.xml (834 bytes).
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.fingerprint.xml

# --- connsys power policy (F4406): wmt_launcher without -o 1 (no always-power-on) ---
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/init/init.mindone-connsys.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.mindone-connsys.rc
