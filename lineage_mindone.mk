#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Adapted from a LineageOS device tree for the same chipset.
# Identity below is OURS, PROVEN (read from the device): ro.product.brand=iKKO,
# ro.product.manufacturer=iKKO, ro.product.model=MindOne, ro.build.fingerprint=
# iKKO/MindOne/MindOne:15/AP3A.240905.015.A2/1776068888:user/release-keys

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from device makefile.
$(call inherit-product, device/ikko/mindone/device.mk)

# Inherit some common LineageOS stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_mindone
PRODUCT_DEVICE := mindone
PRODUCT_MANUFACTURER := iKKO
PRODUCT_BRAND := iKKO
PRODUCT_MODEL := MindOne

# 🔴 OPEN: PRODUCT_GMS_CLIENTID_BASE - no identifier of our own is set here; not critical
# for the first small image (a GMS-specific identifier), fill in if it turns out to be needed.

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildFingerprint=iKKO/MindOne/MindOne:15/AP3A.240905.015.A2/1776068888:user/release-keys \
    DeviceProduct=mindone

# F3807: the stock MTK privileged apps are missing declared permissions, and under
# `enforce` they crash. The value is set HERE, after both inherits, because LineageOS
# adds its own `enforce` to the same variable, and the new build system raises an
# `found duplicate sysprop assignments` error on a duplicate key instead of the old "last one wins".
# So the other value is pushed out with filter-out instead of being overridden.
PRODUCT_PRODUCT_PROPERTIES := $(filter-out ro.control_privapp_permissions=%,$(PRODUCT_PRODUCT_PROPERTIES))
PRODUCT_PRODUCT_PROPERTIES += ro.control_privapp_permissions=log

# --- GApps (GAPPS-PLAN, WITH_GAPPS=true) ---
# 13.09: on by default for the daily-driver ROM. Play Store as a plain /data app crashes on Android 16
# (FGS systemExempted denial F4288, then "MANAGE_USERS ... query users" 13.09 16:44) — it must be the
# privileged product app MindTheGapps installs, with its privapp-permissions allowlists. vendor/gapps
# is cloned in the build tree by tools/scripts/gapps-integrate-vendor.sh (branch baklava).
WITH_GAPPS ?= true
ifeq ($(WITH_GAPPS),true)
$(call inherit-product, vendor/gapps/arm64/arm64-vendor.mk)
endif
