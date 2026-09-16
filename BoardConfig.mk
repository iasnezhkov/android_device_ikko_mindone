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

# Bootloader - F3414 (01.09): this name is an ODM reference-design codename and is device-specific,
# so it has to be read off the device rather than assumed: `strings` on a dump of the device's preloader partition
# reveals embedded build paths `/media/disk4/lhy/G251_GMS_IR/MT8781_S/vendor/mediatek/...`, where
# `G251` is the reference-design codename and "GMS_IR" is likely a build variant rather than part of
# the board name. 🔴 Medium confidence - found
# in the preloader's strings (the same kind of source that gave the correct offsets, F3407), but NOT
# cross-checked directly against `fastboot getvar` on the live device (unavailable on 01.09, asleep).
TARGET_BOOTLOADER_BOARD_NAME := G251
TARGET_BOARD_FASTBOOT_INFO_FILE := $(DEVICE_PATH)/fastboot-info.txt
TARGET_NO_BOOTLOADER := true

# Boot Image - these offsets are VERIFIED against the addresses measured on this device (F3406,
# 01.09, fixes an arithmetic error in F3403). The subtlety that caused that error: an offset is
# measured from BOARD_KERNEL_BASE (mkbootimg: address = BASE + OFFSET), NOT from the absolute
# kernel_addr. Checking the pair below against the device: BASE=0x3fff8000+OFFSET=0x8000 gives
# exactly the measured kernel_addr=0x40000000; RAMDISK_OFFSET=0x26f08000 from the same BASE gives
# exactly ramdisk_addr=0x66f00000; TAGS/DTB_OFFSET=0x07c88000 gives exactly
# tags_addr=dtb_addr=0x47c80000. All four match byte-for-byte.
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
# Everything below mirrors the header cmdline of the daily vendor_boot as of 02.09.2026
# (vendor_boot-0209lk7): pstore/ramoops geometry (F3493), kmsg writable for user space
# (bpfloader/init visibility), the display taken over from the bootloader at runtime
# (F3498), teo cpuidle governor (F3507). hung_task_panic is the candidate-testing default;
# revisit for the product.
BOARD_KERNEL_CMDLINE += pd_ignore_unused printk.devkmsg=on
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
# 06.09 (K3/F3800): tokens that used to live only in the old patched DTB's bootargs — now in the header, so the
# clean tree DTB (f23111e3) can replace it. a personal LAN-IP Wi-Fi override (mindone_ip) deliberately NOT carried over.
BOARD_KERNEL_CMDLINE += fw_devlink=permissive log_buf_len=16M mindone_rst_level=-1 wlan_drv_gen4m_6789.mindone_delay_ms=14000
# 12.09 (F4236/F4238): the GPU bandwidth monitor (stock GPU->SSPM QoS path) is enabled at mali probe via a parameter;
# without it DVFSRC gets no GPU-info (a side effect of ). Verified with a 4/4 series on B17e/B18b. This first-stage
# parameter can only be set via cmdline (the module loads from vendor_boot before /vendor).
BOARD_KERNEL_CMDLINE += mali_kbase_mt6789.mindone_gpu_bm=1
# 12.09 night: UFS clock scaling (F4256) is present in the kernel/DT but NOT enabled by cmdline.
# The UX rig (F4279/F4280, 4x30 min) proved down-scaling storage to 26 MHz causes slow app launches and
# >700 ms Davey frames; storage stays at the fixed 192 MHz parent (as B18b) for a lag-free feel. Re-enable
# 13.09 (F4282): UFS devfreq clock scaling (26/192 MHz, F4256) is back on. The interactive lag it caused
# (F4279/F4280) is avoided by mindone_ufs_screen (set 1309c): floor 192 MHz via PM QoS while the display is
# on, devfreq free to scale while the display is off (verified on B36).
# 12.09 night (F4267, 6.18 review C5): lazy RCU callbacks, batches wakeups in idle; compiled RCU_LAZY default off.
BOARD_KERNEL_CMDLINE += rcutree.enable_rcu_lazy=1
BOARD_KERNEL_CMDLINE += ufs_mediatek.mindone_clkscale=1
# 🔴 12.09: clk_ignore_unused - was present on the command line of every working 6.12 image (F4161, device ran 21h with no
# reboots); without it the kernel gates "unused" clocks after boot (F257). The ROM did not carry it before.
BOARD_KERNEL_CMDLINE += clk_ignore_unused
# vibrator LED class name for LineageOS vibrator-service.legacy (F3812); stock _a keeps aw_vibrator by default
BOARD_KERNEL_CMDLINE += haptic_nv.vib_name=vibrator

BOARD_MKBOOTIMG_ARGS := --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_MKBOOTIMG_ARGS += --dtb_offset $(BOARD_DTB_OFFSET)
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_MKBOOTIMG_ARGS += --board ""

# Density - ✅ PROVEN 01.09 (F3415): ro.sf.lcd_density=400, read from the device -
# our real value, read from the device rather than assumed from a panel of a similar class.
# 🔴 07.09: 400 -> 360 for our SHORT screen (the screen-adaptation notes).
# At 400 the screen gives 432 x 496 dp: width is normal, but height is about half the usual (900-1000).
# 360 gives 480 x 551 dp - plus 11% more vertical space for every app at once, at the cost of 10% smaller elements.
# A moderate value was chosen, not 320: without the device in hand it cannot be checked by touch, and erring
# on the smaller side is cheaper. The value is REVERSIBLE on a live system: `wm density 400` restores the old value,
# `wm density reset` restores the build value. Pick the exact number by eye and record it here.
# ✅ 09.09 CONFIRMED BY MEASUREMENT on the device (F4014): `wm density` -> `Physical density: 360` with
# no `Override` line, `ro.sf.lcd_density` = 360. So 360 really did make it to the system.
# 🔴 16.09: the value below is now 380, not the 360 the paragraphs above describe - raised after those
# notes were written. At 380 the screen is 455 x 522 dp and the app keeps 496 dp once the status bar
# is gone, so everything above that quotes 480 x 551 / 523 dp is one step stale. Anything sized in dp
# against this screen has to be recomputed when this number moves: the on-screen keyboard overlay was
# left at a height chosen for 360 and came out a fifth too small in practice.
# Usable height for the app is 523 dp: gesture navigation, the button bar does not take vertical space,
# only the status bar is lost (63 px).
# 🔴 The first line of this block, about 400, refers to the STOCK DUMP, not to our build. Reading it in
# isolation from the rest is exactly the mistake that caused the screen-adaptation notes to be written for 400.
# MINDONE 12.09: chosen "a bit bigger" than stock - 380 (stock 400, screen 480x551 dp at 360).
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

# Kernel - the PREBUILT path (LINEAGE-PLAN par. 5.1: our kernel pipeline is already
# debugged separately in the VM; TARGET_KERNEL_SOURCE/integrated-build would mean setting it up again
# inside the AOSP kernel/build tooling - more expensive than bringing in an already-built Image).
# 🔴 15.09 (F4449): this MUST be the GZIPPED kernel. MTK LK cannot boot a raw
# 40 MiB arm64 Image - it never reaches the kernel, so ramoops stays empty and
# LK just marks the slot unbootable (slot_a = 0x00). That is exactly why the
# 1409 ROM never booted on _a, and why the 1309 ROM's own boot.img never booted
# either: every "working" install so far was rescued by hand-flashing a boot.img
# whose kernel is the SAME kernel, only gzipped (boot_b's kernel, gunzipped, is
# byte-identical to kernel/Image - sha a1b24afa...). Do not point this back at
# the uncompressed Image.
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/kernel/Image.gz
TARGET_KERNEL_ARCH := arm64

# 12.09: ALL modules for 6.12 (290 at the time of writing) load in the FIRST stage from vendor_boot
# (modules.load.ramdisk); kernel/modules.load (2nd stage, vendor_dlkm) is EMPTY - the previous 36 names were
# a strict subset of the first stage and were loading twice. vendor_dlkm remains a partition with no .ko files.
# The previous list is kernel/modules.load.bak-1209-2ndstage. The description below is 03.09/06.09 history.
# Kernel modules — the two stages of the module set (first-stage ramdisk, second-stage vendor_dlkm):
#   modules.load.ramdisk  — 1st stage, vendor_boot ramdisk (244 modules, the image's modules.load)
#   modules.load          — 2nd stage, vendor_dlkm (38: mcupm, fhctl, cpufreq-hw, LPM, ...; order matters,
#                           F3505: cpufreq-hw after mcupm)
#   modules.load.recovery — recovery ramdisk subset (122; the 20 stock-5.10 names that do not exist in
#                           our 6.1 set are built into the Image or renamed)
# kernel/modules/*.ko is the union of both stages from the kernel tree<tag> (build
# products, .gitignore'd like Image/dtb/dtbo.img — the public device tree will take them from a
# kernel-prebuilt repo, REPO-LAYOUT-PLAN-0902 D0).
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/kernel/modules.load.ramdisk))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/kernel/modules.load.recovery))
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/kernel/modules.load))
BOOT_KERNEL_MODULES := $(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD) $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD)
BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(foreach m,$(BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD),$(DEVICE_PATH)/kernel/modules/$(m))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES := $(foreach m,$(BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD),$(DEVICE_PATH)/kernel/modules/$(m))
# 06.09 (F3820): vendor_dlkm = ONLY the 2nd-stage modules (the modules.load list, 40), not all 283 -
# otherwise the 38 MB image does not fit in the stock vendor_dlkm region (65648 sectors @12421120, F1257) and
# OTA spills it in tails across super; the 1st stage is already entirely in vendor_boot.
BOARD_VENDOR_KERNEL_MODULES := $(foreach m,$(BOARD_VENDOR_KERNEL_MODULES_LOAD),$(DEVICE_PATH)/kernel/modules/$(m))

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

# Partitions - our exact sizes, PROVEN (stock-firmware-inventory,
# F3363, a fresh lpdump of this device)
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := $(BOARD_BOOTIMAGE_PARTITION_SIZE)
BOARD_DTBOIMG_PARTITION_SIZE := 8388608
BOARD_SUPER_PARTITION_SIZE := 9663676416
BOARD_SUPER_IMAGE_IN_UPDATE_PACKAGE := true
BOARD_USES_METADATA_PARTITION := true

# Partitions (Dynamic) - the list is PROVEN exactly (F3363, 01.09): odm_dlkm/product/system/
# system_ext/vendor/vendor_dlkm, all 6 are physically present (odm_dlkm is nearly empty,
# 0.0003 GiB, but the partition is real)
BOARD_SUPER_PARTITION_GROUPS := mediatek_dynamic_partitions
BOARD_MEDIATEK_DYNAMIC_PARTITIONS_PARTITION_LIST := odm_dlkm product system system_ext vendor vendor_dlkm
# 🔴 F3417: the common "group size = super/2" rule does NOT apply here - the real lpdump of this
# device (its own lpdump)
# shows TWO groups main_a/main_b, each with Maximum size 9661579264 bytes (approx. the whole super, NOT half).
# Halving would give 4.5 GiB per group, and K1 (7.01 GiB, F3409) would not fit. We use the real
# number. The generic MediaTek group name (mediatek_dynamic_partitions) vs the real one (main_a/main_b) is
# NOT fully cross-checked (could be an MTK build-time alias, auto-suffixed by slot) - check
# at the first real repartition, not before.
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
#   2. It was set to make `adb reboot recovery` work. It cannot. This bootloader puts
#      androidboot.force_normal_boot=1 on the kernel command line unconditionally and never
#      reads the boot-recovery command out of the BCB, so the request is dropped before any
#      ramdisk is chosen. Measured: after `adb reboot recovery` the device comes back up in the
#      normal system with force_normal_boot="1" in /proc/cmdline and an empty BCB.
#
# What it did do is split one fragment into two, which is a layout this device has never been
# booted with. Leave it off: the flashing procedure goes through fastboot (see docs/FLASHING.md),
# not recovery.
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

# Verified Boot - test AVB keys, as there is no signing chain of our own
# (NOTE: F3364 - on the STOCK firmware verity/verification are currently
# ENABLED, this is about the existing vbmeta, not about this future from-scratch build)
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
# 06.09 (F3839): private wpa_supplicant DRIVER commands (SETSUSPENDMODE/COUNTRY/SETBAND -> gen4m via SIOCDEVPRIVATE+1);
# without this Lineage links libdrivercmdfallback and CONNSYS wakes the AP 2.6/s while idle (F3838). Module: wifi/wpa_supplicant_8_lib.
# The soong variable derived from it is set automatically by external/wpa_supplicant_8/board_config_wpa_supplicant.mk (included
# from build/make/core/board_config.mk:294) - no need to call soong_config_set by hand.
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

# 🔴 15.09 (F4461): REVERTED. Building android.hardware.audio.service 64-bit KILLED ALL SOUND, and
# this is the whole story of "no audio on ROM 1409".
#
# The 13.09 reasoning below was "the 64-bit MediaTek libraries exist and resolve completely
# (linker64 --list ... none missing), so the 64-bit service can run the same stock blobs". Resolving
# is not the test. What decides is which primary module the loader FINDS, and it walks a property
# chain that ends at ro.board.platform = mt6789:
#     /vendor/lib/hw/audio.primary.mt6789.so     EXISTS   <- 32-bit service loads this, real MTK HAL
#     /vendor/lib64/hw/audio.primary.mt6789.so   MISSING  <- 64-bit service falls through...
# ...to audio.primary.default.so, the STUB. The stub accepts every write and reports success, so
# AudioFlinger looks perfectly healthy (Standby: no, frames written climbing) while the kernel sees
# nothing at all: no AFE hw_params, I2S3 switches off, speaker amp off. Silence with no error.
# Pointing the property at the one 64-bit module that does exist (audio.primary.mediatek.so) is not
# a way out either - it SIGSEGVs the HAL service at pc=0 and audioserver then crash-loops through
# boot (measured 15.09, tombstone_11).
#
# Timeline that proves it: ROM 1309 was built 13.09 11:56, this commit landed 13.09 16:18, ROM 1409
# was built 14.09 19:20. Sound worked on 1309 and died on 1409 - exactly the builds either side.
#
# 🔴 15.09, SECOND PASS: re-enabled, because the precondition above is now met. The reason 64-bit
# failed was never the bit width - it was that no 64-bit audio.primary existed for this board, so
# the loader fell through to the stub. We now ship one: audio.primary.mindone is our own source,
# built for both ABIs, and vendor.prop names it explicitly through ro.hardware.audio.primary, so
# the loader no longer has to guess. The MediaTek 64-bit blob stays unused - it corrupts its own
# stack (F4464) and is not on any path now.
$(call soong_config_set,android_hardware_audio,run_64bit,true)
