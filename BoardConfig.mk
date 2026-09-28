#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Adapted from a LineageOS device tree for the same MT6789
# chipset) - LINEAGE-PLAN par. 5/6.3 item 4. Values below are either (a) VERIFIED against
# our own device (marked "ours, PROVEN", with the fact/doc), (b) chipset-generic and kept
# as any MT6789/Helio G99 platform needs it (not device-specific), or (c) explicitly
# marked OPEN/TODO where we genuinely don't have our own number yet - do not silently fill
# those in with a number taken from another device's tree: two such values were caught here and were
# WRONG for us (BOARD_RAMDISK_OFFSET, BOARD_KERNEL_TAGS_OFFSET/BOARD_DTB_OFFSET).

DEVICE_PATH := device/ikko/mindone

# Architecture — chipset-generic (Helio G99 = MT6789 = 2×Cortex-A76 + 6×Cortex-A55)
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-2a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := cortex-a76

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-2a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
TARGET_2ND_CPU_VARIANT_RUNTIME := cortex-a55
TARGET_DYNAMIC_64_32_MEDIASERVER := true

# A/B
AB_OTA_UPDATER := true

AB_OTA_PARTITIONS += \
    boot \
    vendor_boot \
    dtbo \
    system \
    system_ext \
    product \
    vendor \
    vendor_dlkm \
    odm_dlkm \
    vbmeta \
    vbmeta_system \
    vbmeta_vendor

TARGET_BOOTLOADER_BOARD_NAME := G251
TARGET_BOARD_FASTBOOT_INFO_FILE := $(DEVICE_PATH)/fastboot-info.txt
TARGET_NO_BOOTLOADER := true

BOARD_KERNEL_BASE := 0x3fff8000
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_KERNEL_TAGS_OFFSET := 0x07c88000
BOARD_RAMDISK_OFFSET := 0x26f08000
BOARD_DTB_OFFSET := 0x07c88000

BOARD_BOOT_HEADER_VERSION := 4
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_MOVE_GSI_AVB_KEYS_TO_VENDOR_BOOT := true
BOARD_USES_GENERIC_KERNEL_IMAGE := true

BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2
BOARD_KERNEL_CMDLINE += printk.devkmsg=on
BOARD_KERNEL_CMDLINE += ramoops.mem_address=0x9f670000 ramoops.mem_size=0xe0000 ramoops.console_size=0x90000 ramoops.record_size=0x2000 ramoops.pmsg_size=0x40000 ramoops.ftrace_size=0x2000
BOARD_KERNEL_CMDLINE += loglevel=5 hung_task_panic=0 hung_task_timeout_secs=45 mrdump.mindone_no_dump=1
# SELinux runs enforcing, as LineageOS expects. The permissive cmdline that used to sit here was
# added 04.09 to debug why the build would not boot at all, with a note to remove it once it did;
# the build has booted since 09.09 and the note outlived its reason. Leaving it in meant every
# daily image ran with SELinux effectively off, which both CTS (SELinuxHostTest) and VTS check
# directly.
#
# Removed 15.09 after measuring it live rather than assuming: `setenforce 1` on the running device,
# then audio recording, camera and Settings exercised. Twenty denials, every one of them an app
# probing something it cannot have - adbd_prop, mnt_sdcard_file, content_capture_service,
# proc_version - and not one from a system or vendor domain, ours included. The modem stayed
# ready and audioserver stayed up throughout.
BOARD_KERNEL_CMDLINE += mediatek_drm.mindone_lk_takeover=1 cpuidle.governor=teo
BOARD_KERNEL_CMDLINE += log_buf_len=16M
BOARD_KERNEL_CMDLINE += rcutree.enable_rcu_lazy=1
BOARD_KERNEL_CMDLINE += haptic_nv.vib_name=vibrator
BOARD_KERNEL_CMDLINE += ioremap_guard kasan.page_alloc.sample=10

BOARD_MKBOOTIMG_ARGS := --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_MKBOOTIMG_ARGS += --dtb_offset $(BOARD_DTB_OFFSET)
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_MKBOOTIMG_ARGS += --board ""

TARGET_SCREEN_DENSITY := 380

# DTBO
BOARD_KERNEL_SEPARATED_DTBO := true

# HIDL
DEVICE_MANIFEST_FILE += $(DEVICE_PATH)/manifest.xml
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE := hardware/mediatek/vintf/mediatek_framework_compatibility_matrix.xml
# Plus our matrix: four vendor HALs (NXP eSE/NFC, Skyroam) are declared
# optional. Without it assemble_vintf declares the target INCOMPATIBLE and OTA
# packaging fails - the check itself recommends exactly this approach for device-specific HALs.
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += $(DEVICE_PATH)/framework_compatibility_matrix.xml

TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/kernel/Image.gz
TARGET_KERNEL_ARCH := arm64

BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/kernel/modules.load.ramdisk))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/kernel/modules.load.recovery))
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/kernel/modules.load))
BOOT_KERNEL_MODULES := $(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD) $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD)
BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(foreach m,$(sort $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD) $(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD)),$(DEVICE_PATH)/kernel/modules/$(m))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES := $(foreach m,$(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD),$(DEVICE_PATH)/kernel/modules/$(m))
BOARD_VENDOR_KERNEL_MODULES := $(foreach m,$(BOARD_VENDOR_KERNEL_MODULES_LOAD),$(DEVICE_PATH)/kernel/modules/$(m))
BOARD_VENDOR_KERNEL_MODULES_OPTIONS_FILE := $(DEVICE_PATH)/kernel/modules.options
BOARD_DO_NOT_STRIP_VENDOR_RAMDISK_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_MODULES := true
BOARD_DO_NOT_STRIP_RECOVERY_MODULES := true

# DTB / DTBO — dtb from the GOOD vendor_boot (same one the flashed image carries), dtbo from the
# stock dtbo partition trimmed to its dt-table total_size (read from the device)
BOARD_PREBUILT_DTBIMAGE_DIR := $(DEVICE_PATH)/kernel/dtb
# 🔴 Exactly one blob, and the build says so rather than finding out on the phone.
# This directory is concatenated whole: every *.dtb in it is appended, in glob order, into the
# blob that goes into vendor_boot. It is a build product and .gitignore'd, so an older blob under
# a different name survives a clean checkout, sits next to the new one, and the device tree ends
# up in the image twice -- byte-identical halves, no warning from anything. The phone then does
# not boot: no adb, a preloader window every ~33 s, and nothing in pstore to say why. That cost
# an hour to find, and the only visible clue was the dtb inside vendor_boot being exactly twice
# the size of the blob that was built.
mindone_dtbs := $(wildcard $(BOARD_PREBUILT_DTBIMAGE_DIR)/*.dtb)
ifneq ($(words $(mindone_dtbs)),1)
ifneq ($(words $(mindone_dtbs)),0)
$(error $(BOARD_PREBUILT_DTBIMAGE_DIR) holds $(words $(mindone_dtbs)) .dtb files ($(notdir $(mindone_dtbs))); \
        every one of them is concatenated into vendor_boot. Keep exactly one and rebuild)
endif
endif
BOARD_PREBUILT_DTBOIMAGE := $(DEVICE_PATH)/kernel/dtbo.img

BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := $(BOARD_BOOTIMAGE_PARTITION_SIZE)
BOARD_DTBOIMG_PARTITION_SIZE := 8388608
BOARD_SUPER_PARTITION_SIZE := 9663676416
BOARD_SUPER_IMAGE_IN_UPDATE_PACKAGE := true
BOARD_USES_METADATA_PARTITION := true

BOARD_SUPER_PARTITION_GROUPS := mediatek_dynamic_partitions
BOARD_MEDIATEK_DYNAMIC_PARTITIONS_PARTITION_LIST := odm_dlkm product system system_ext vendor vendor_dlkm
BOARD_MEDIATEK_DYNAMIC_PARTITIONS_SIZE := 9661579264

BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDOR_DLKMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_ODM_DLKMIMAGE_FILE_SYSTEM_TYPE := ext4

TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_SYSTEM_EXT := system_ext
TARGET_COPY_OUT_VENDOR := vendor
TARGET_COPY_OUT_VENDOR_DLKM := vendor_dlkm
TARGET_COPY_OUT_ODM_DLKM := odm_dlkm

# Properties
TARGET_PRODUCT_PROP += $(DEVICE_PATH)/product.prop
TARGET_SYSTEM_EXT_PROP += $(DEVICE_PATH)/system_ext.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Recovery - most MT6789 trees name this fstab.mt6789, but on this device ro.hardware is
# mt8781, so init resolves fstab.${ro.hardware} to fstab.mt8781. A path carried over from
# another tree will not resolve; the file this points at is in rootdir/etc/.
BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT := true
# The recovery resources land in the single platform ramdisk fragment, and that is deliberate.
#
# BOARD_INCLUDE_RECOVERY_RAMDISK_IN_VENDOR_BOOT was set here for one build and then reverted,
# because both halves of the reasoning behind it turned out to be wrong:
#
#   1. It was set to "finally pack the recovery resources". They were never missing. The image
#      built without it carries one type 0x1 fragment holding all 875 files, recovery binaries
#      included -- system/bin/recovery, adbd, recovery.fstab, with ro.adb.secure=0. Unpacking
#      the vendor_boot off the device proved it.
#
#   2. It was set to make `adb reboot recovery` work. The fragment layout has nothing to do with
#      that: this LK loads the whole vendor ramdisk whatever the fragment types. LK does not read
#      the BCB at all; it picks recovery from the RGU register NONRST2 (0x10007024, low nibble 2),
#      written by syscon-reboot-mode on `reboot recovery`, by `fastboot reboot recovery`, or by the
#      key menu (top-left key + Power from power-off, first item). Only in that mode does it leave out
#      androidboot.force_normal_boot=1 (IDA on our lk: boot_linux_fdt, mode 2 skips the append).
#
# What it did do is split one fragment into two, which is a layout this device has never been
# booted with. Leave it off.
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.mt8781
TARGET_RECOVERY_PIXEL_FORMAT := BGRA_8888
TARGET_USERIMAGES_USE_F2FS := true

# SEPolicy
include device/mediatek/sepolicy_vndr/SEPolicy.mk
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor

# SPL - ours, PROVEN (DEVICE-MAP, ro.build.version.security_patch from
# read from the device)
BOOT_SECURITY_PATCH := 2026-03-05
VENDOR_SECURITY_PATCH := $(BOOT_SECURITY_PATCH)

BOARD_AVB_ENABLE := true

ifneq (,$(AVB_CUSTOM_KEY_PATH))
BOARD_AVB_ALGORITHM := $(AVB_CUSTOM_ALGORITHM)
BOARD_AVB_KEY_PATH := $(AVB_CUSTOM_KEY_PATH)
else
AVB_CUSTOM_ALGORITHM := SHA256_RSA2048
AVB_CUSTOM_KEY_PATH := external/avb/test/data/testkey_rsa2048.pem
endif

ifneq ($(WITH_AVB),true)
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
endif

BOARD_AVB_BOOT_ALGORITHM := $(AVB_CUSTOM_ALGORITHM)
BOARD_AVB_BOOT_KEY_PATH := $(AVB_CUSTOM_KEY_PATH)
BOARD_AVB_BOOT_ROLLBACK_INDEX := 1
BOARD_AVB_BOOT_ROLLBACK_INDEX_LOCATION := 1

BOARD_AVB_VBMETA_SYSTEM := product system system_ext
BOARD_AVB_VBMETA_SYSTEM_ALGORITHM := $(AVB_CUSTOM_ALGORITHM)
BOARD_AVB_VBMETA_SYSTEM_KEY_PATH := $(AVB_CUSTOM_KEY_PATH)
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX := 1
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX_LOCATION := 2

BOARD_AVB_VBMETA_VENDOR := vendor
BOARD_AVB_VBMETA_VENDOR_ALGORITHM := $(AVB_CUSTOM_ALGORITHM)
BOARD_AVB_VBMETA_VENDOR_KEY_PATH := $(AVB_CUSTOM_KEY_PATH)
BOARD_AVB_VBMETA_VENDOR_ROLLBACK_INDEX := 1
BOARD_AVB_VBMETA_VENDOR_ROLLBACK_INDEX_LOCATION := 3

# Wi-Fi - chipset-generic MediaTek WMT/CONNSYS path (wmtWifi), matches our own
# WMT/CONNSYS driver (HANDOFF track 3, wlan_drv_gen4m_6789) - not device-specific
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_DRIVER := NL80211
WIFI_DRIVER_FW_PATH_STA := "STA"
WIFI_DRIVER_FW_PATH_AP := "AP"
WIFI_DRIVER_FW_PATH_P2P := "P2P"
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true
WIFI_DRIVER_FW_PATH_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_STATE_CTRL_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_STATE_ON := "1"
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mt66xx
WIFI_DRIVER_STATE_OFF := "0"

# Inherit the proprietary files
include vendor/ikko/mindone/BoardConfigVendor.mk

# 🔴 04.09: symbol checking for PREBUILT vendor libraries is disabled.
# The build failed twice on different blobs - `libaalservice.so`, then `libkeymint_mtk.so`
# (the latter has mismatched `BnRemotelyProvisionedComponent` symbols: the blob was built by the
# manufacturer against a different version of the KeyMint interface). There is no source, it cannot
# be rebuilt, and the blob list has 608 entries - targeted exclusions would mean one
# re-run of the build for every blob found.
# This is specifically this variable and not `PRODUCT_CHECK_ELF_FILES`: the latter is marked deprecated
# (`build/make/core/config.mk:166`, KATI_obsolete_var) and would fail the build outright.
# It applies both in make (`check_elf_file.mk:65`) and in Soong - `soong_config.mk:304` passes
# it through as `BuildBrokenPrebuiltELFFiles`, and our failure was coming specifically from Soong.
BUILD_BROKEN_PREBUILT_ELF_FILES := true

$(call soong_config_set,android_hardware_audio,run_64bit,true)
BUILD_BROKEN_NINJA_USES_ENV_VARS += ROSETTA_DISABLE_AOT
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
