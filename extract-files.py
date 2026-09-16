#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Adapted from a LineageOS device tree for the same chipset (MT6789). blob_fixups/
# lib_fixups below are MT6789-platform/AOSP-ABI renames (keymint-trustkernel, wifi-hal,
# codec2 formatter, sensors hals.conf, ...), not specific to that device — several are
# already confirmed present on OUR device (e.g. android.hardware.security.keymint-
# service.trustkernel, seen live in our own boot log F3400/F3403). Kept verbatim until
# proven wrong against a dump of this device's own partitions.
# The blob lists have since been reconciled against a dump taken from this device: of the
# entries in proprietary-files.txt, the large majority were confirmed present on the real
# partition, and entries that are deliberately not taken carry the reason inline.
from audio_param_tuning import (
    tune_speech_audio_param,
    tune_speech_vol_audio_param,
)

from extract_utils.fixups_blob import (
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'device/ikko/mindone',
    'hardware/mediatek',
    'hardware/mediatek/libmtkperf_client',
]


def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None


lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    ('vendor.mediatek.hardware.videotelephony@1.0'): lib_fixup_vendor_suffix,
}

def fixup_speech_audio_param(ctx, file, file_path, *args, **kwargs):
    """Switch on the handset uplink DRC in the device's own speech tables."""
    tune_speech_audio_param(file_path)


def fixup_speech_vol_audio_param(ctx, file, file_path, *args, **kwargs):
    """Back off the handset uplink gain in the device's own volume tables."""
    tune_speech_vol_audio_param(file_path)


blob_fixups: blob_fixups_user_type = {
    # The speech tuning tables are MediaTek's and are not kept in this repository. They are
    # extracted from the device and then adjusted in place: see audio_param_tuning.py for the
    # three values that are ours and the reasoning behind each.
    (
        'vendor/etc/audio_param/Speech_AudioParam.xml',
    ): blob_fixup()
        .call(fixup_speech_audio_param, need_tmp_dir=False),
    (
        'vendor/etc/audio_param/SpeechVol_AudioParam.xml',
    ): blob_fixup()
        .call(fixup_speech_vol_audio_param, need_tmp_dir=False),

    # ImsService.apk: the apktool patch in ims-patches/ does not apply to this device's
    # ImsService (hunk #2 rejected, 03.09); IMS/VoLTE is a placeholder on kernel 6.1 (F2574),
    # so the apk is copied as-is until IMS is brought up.
    (
        'vendor/bin/hw/android.hardware.gnss-service.mediatek',
        'vendor/lib64/hw/android.hardware.gnss-impl-mediatek.so',
    ): blob_fixup()
        .replace_needed('android.hardware.gnss-V1-ndk_platform.so', 'android.hardware.gnss-V1-ndk.so'),
    (
        'vendor/bin/mnld',
    ): blob_fixup()
        .binary_regex_replace(b'ro.logsystem.usertype', b'ro.vendor.lgsys.utype')
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so')
        .replace_needed('libmnl.so', 'libmnl_mtk.so'),
    (
        'vendor/firmware/ocs72xxx_cf.bin'
    ): blob_fixup()
        .binary_regex_replace(b"\x5F\x00\x00\x00", b"\x40\x00\x00\x00")
        .binary_regex_replace(
            b"\x01\x0D\x02\x1C\x03\x08\x04\x09\x05\x0E\x06\x08\x07\x56\x08\x87\x09\x12\x0A\xCA\x0B\x26",
            b"\x01\x0D\x02\x1C\x03\x06\x04\x09\x05\x0A\x06\x06\x07\x56\x08\x87\x09\x12\x0A\xCA\x0B\x26",
        )
        .binary_regex_replace(
            b"\x01\x0D\x02\x1C\x03\x08\x04\x09\x05\x0E\x06\x08",
            b"\x01\x0D\x02\x1C\x03\x06\x04\x09\x05\x0A\x06\x06",
        ),
    (
        'vendor/lib/libcodec2_mtk_venc.so',
        'vendor/lib/libcodec2_mtk_vdec.so',
        'vendor/lib64/libcodec2_mtk_venc.so',
        'vendor/lib64/libcodec2_mtk_vdec.so',
    ): blob_fixup()
        .replace_needed('libformatter.so', 'libformatter_mtk.so'),
    (
        'vendor/lib/libformatter_mtk.so',
        'vendor/lib64/libformatter_mtk.so',
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib64/android.hardware.power-service-mediatek.so'
    ): blob_fixup()
        .replace_needed('android.hardware.power-V2-ndk_platform.so', 'android.hardware.power-V2-ndk.so'),
    (
        'vendor/lib64/hw/hwcomposer.mtk_common.so'
    ): blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    (
        'vendor/lib64/lib3a.flash.so',
        'vendor/lib64/lib3a.ae.stat.so',
        'vendor/lib64/lib3a.sensors.color.so',
        'vendor/lib64/lib3a.sensors.flicker.so',
    ): blob_fixup()
        .add_needed('liblog.so'),
    (
        'vendor/lib/libGsFace_ca.so',
        'vendor/lib64/libGsFace_ca.so',
    ): blob_fixup()
        .add_needed('libteec.so'),
    (
        'vendor/lib64/libcam.utils.sensorprovider.so',
    ): blob_fixup()
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so'),
    (
        'vendor/lib64/libmnl_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .add_needed('libcutils.so'),
    (
        'vendor/lib/libneuralnetworks_sl_driver_mtk_prebuilt.so',
        'vendor/lib64/libneuralnetworks_sl_driver_mtk_prebuilt.so',
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock')
        .clear_symbol_version('AHardwareBuffer_createFromHandle')
        .clear_symbol_version('AHardwareBuffer_getNativeHandle'),
    (
        'vendor/bin/nfcstackp-vendor',
        'vendor/bin/STFlashTool',
        'vendor/lib/libnvram.so',
        'vendor/lib64/libnvram.so',
        'vendor/lib/libsysenv.so',
        'vendor/lib64/libsysenv.so',
    ): blob_fixup()
        .add_needed('libbase_shim.so'),
    (
        'system_ext/lib64/libsink-mtk.so',
        'vendor/lib/libspeech_enh_lib.so',
        'vendor/lib64/libspeech_enh_lib.so',
        'vendor/lib64/libwifi-hal-mtk.so',
        'vendor/lib64/hw/sensors.mt6789.so',
        'vendor/lib/hw/audio.primary.mt6789.so',
        'vendor/lib64/hw/audio.primary.mt6789.so',
        'vendor/lib/hw/audio.r_submix.mt6789.so',
        'vendor/lib64/hw/audio.r_submix.mt6789.so',
    ): blob_fixup()
        .fix_soname(),
    # The TEE keymint service (trustkernel) and the stock keymint/keymaster libraries it links are
    # built against keymint AIDL V1; the tree's own libkeymint stack is V4 and shares the names and
    # vendor install paths, so the 8 stock libs are shipped renamed *_mtk (fix_soname + replace_needed
    # through the whole closed set; NEEDED graph verified against the dump, 03.09). _platform AIDL
    # sonames are mapped to the frozen V1 -ndk libraries the tree builds.
    (
        'vendor/bin/hw/android.hardware.security.keymint-service.trustkernel'
    ): blob_fixup()
        .replace_needed('android.hardware.security.keymint-V1-ndk_platform.so', 'android.hardware.security.keymint-V1-ndk.so')
        .replace_needed('android.hardware.security.secureclock-V1-ndk_platform.so', 'android.hardware.security.secureclock-V1-ndk.so')
        .replace_needed('android.hardware.security.sharedsecret-V1-ndk_platform.so', 'android.hardware.security.sharedsecret-V1-ndk.so')
        .replace_needed('libkeymint.so', 'libkeymint_mtk.so')
        .replace_needed('libcppbor_external.so', 'libcppbor_external_mtk.so')
        .add_needed('android.hardware.security.rkp-V1-ndk.so'),
    (
        'vendor/lib64/libkeymint_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .replace_needed('android.hardware.security.keymint-V1-ndk_platform.so', 'android.hardware.security.keymint-V1-ndk.so')
        .replace_needed('android.hardware.security.secureclock-V1-ndk_platform.so', 'android.hardware.security.secureclock-V1-ndk.so')
        .replace_needed('android.hardware.security.sharedsecret-V1-ndk_platform.so', 'android.hardware.security.sharedsecret-V1-ndk.so')
        .replace_needed('lib_android_keymaster_keymint_utils.so', 'lib_android_keymaster_keymint_utils_mtk.so')
        .replace_needed('libcppbor_external.so', 'libcppbor_external_mtk.so')
        .replace_needed('libkeymaster_messages.so', 'libkeymaster_messages_mtk.so')
        .replace_needed('libkeymaster_portable.so', 'libkeymaster_portable_mtk.so')
        .replace_needed('libpuresoftkeymasterdevice.so', 'libpuresoftkeymasterdevice_mtk.so'),
    (
        'vendor/lib64/lib_android_keymaster_keymint_utils_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .replace_needed('android.hardware.security.keymint-V1-ndk_platform.so', 'android.hardware.security.keymint-V1-ndk.so'),
    (
        'vendor/lib64/libkeymaster_messages_mtk.so'
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib64/libkeymaster_portable_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .replace_needed('libcppbor_external.so', 'libcppbor_external_mtk.so')
        .replace_needed('libcppcose_rkp.so', 'libcppcose_rkp_mtk.so'),
    (
        'vendor/lib64/libcppbor_external_mtk.so'
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib64/libcppcose_rkp_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .replace_needed('libcppbor_external.so', 'libcppbor_external_mtk.so'),
    (
        'vendor/lib64/libpuresoftkeymasterdevice_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .replace_needed('libkeymaster_messages.so', 'libkeymaster_messages_mtk.so')
        .replace_needed('libkeymaster_portable.so', 'libkeymaster_portable_mtk.so')
        .replace_needed('libsoft_attestation_cert.so', 'libsoft_attestation_cert_mtk.so')
        .replace_needed('libcppbor_external.so', 'libcppbor_external_mtk.so')
        .replace_needed('libcppcose_rkp.so', 'libcppcose_rkp_mtk.so'),
    (
        'vendor/lib64/libsoft_attestation_cert_mtk.so'
    ): blob_fixup()
        .fix_soname()
        .replace_needed('libkeymaster_portable.so', 'libkeymaster_portable_mtk.so'),
    (
        'vendor/bin/hw/android.hardware.media.c2@1.2-mediatek',
        'vendor/bin/hw/android.hardware.media.c2@1.2-mediatek-64b',
    ): blob_fixup()
        .replace_needed('libavservices_minijail_vendor.so', 'libavservices_minijail.so')
        .add_needed('libstagefright_foundation-v33.so'),
    (
        'vendor/bin/hw/mtkfusionrild',
        'vendor/lib/libmtkcam_stdutils.so',
        'vendor/lib64/libmtkcam_stdutils.so',
        'vendor/lib64/hw/android.hardware.camera.provider@2.6-impl-mediatek.so',
    ): blob_fixup()
        .add_needed('libutils-v32.so'),
    (
        'system_ext/lib64/libimsma.so',
    ): blob_fixup()
        .replace_needed('libsink.so', 'libsink-mtk.so'),
    (
        'system_ext/lib64/libsource.so',
    ): blob_fixup()
        .add_needed('libui_shim.so'),
    (
        'vendor/etc/init/init.volte_md_status.rc'
    ): blob_fixup()
        .binary_regex_replace(b'system/vendor', b'vendor'),
    (
        'vendor/etc/sensors/hals.conf'
    ): blob_fixup()
        .regex_replace('android.hardware.sensors@2.X-subhal-mediatek.so', 'android.hardware.sensors@2.0-subhal-impl-1.0.so'),
}  # fmt: skip

module = ExtractUtilsModule(
    'mindone',
    'ikko',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
    add_firmware_proprietary_file=True,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
