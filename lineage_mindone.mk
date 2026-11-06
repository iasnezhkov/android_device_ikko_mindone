#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

TARGET_SUPPORTS_OMX_SERVICE := false
TARGET_REQUIRES_HIDL_CAS_HAL := false
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

$(call inherit-product, device/ikko/mindone/device.mk)

$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_mindone
PRODUCT_DEVICE := mindone
PRODUCT_MANUFACTURER := iKKO
PRODUCT_BRAND := iKKO
PRODUCT_MODEL := MindOne


PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildFingerprint=iKKO/MindOne/MindOne:15/AP3A.240905.015.A2/1776068888:user/release-keys \
    DeviceProduct=mindone

MINDONE_PERSONAL_BUILD ?= false
MINDONE_COMMUNITY_BUILD ?= false

ifeq ($(MINDONE_COMMUNITY_BUILD),true)
MINDONE_PERSONAL_BUILD := false
WITH_GAPPS := false
endif

ifeq ($(MINDONE_PERSONAL_BUILD),true)
WITH_GAPPS ?= true
else
WITH_GAPPS ?= false
endif

ifeq ($(WITH_GAPPS),true)
$(call inherit-product, vendor/gapps/arm64/arm64-vendor.mk)
PRODUCT_COPY_FILES += \
    device/ikko/mindone/configs/permissions/privapp-permissions-mindone-gms.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/privapp-permissions-mindone-gms.xml
endif
