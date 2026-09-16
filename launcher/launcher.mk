#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Include from the device makefile: $(call inherit-product, <path>/rom/launcher.mk)
PRODUCT_PACKAGES += Launcher

# Device overlay with config_defaultListenerAccessPackages etc.
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay
