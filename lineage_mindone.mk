#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Adapted from a LineageOS device tree for the same chipset.
# Identity below is OURS, PROVEN (read from the device): ro.product.brand=iKKO,
# ro.product.manufacturer=iKKO, ro.product.model=MindOne, ro.build.fingerprint=
# iKKO/MindOne/MindOne:15/AP3A.240905.015.A2/1776068888:user/release-keys

# Inherit from those products. Most specific first.
ZYGOTE_FORCE_64 := true
TARGET_SUPPORTS_OMX_SERVICE := false
TARGET_REQUIRES_HIDL_CAS_HAL := false
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

PRODUCT_PRODUCT_PROPERTIES := $(filter-out ro.control_privapp_permissions=%,$(PRODUCT_PRODUCT_PROPERTIES))
PRODUCT_PRODUCT_PROPERTIES += ro.control_privapp_permissions=log

WITH_GAPPS ?= true
ifeq ($(WITH_GAPPS),true)
$(call inherit-product, vendor/gapps/arm64/arm64-vendor.mk)
endif
