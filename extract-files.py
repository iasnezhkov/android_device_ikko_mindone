#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import os
import re
import shutil
import tempfile
from hashlib import sha1
from pathlib import Path

from extract_utils.file import File, FileArgs, FileList
from extract_utils.module import ProprietaryFile

from audio_param_tuning import (
    tune_speech_audio_param,
    tune_speech_vol_audio_param,
)
from camera_metastore_fixup import fix_imx766_stream_table
from halsensor_poweron_fixup import drop_poweron_sleep
from rxfilter_firmware import firmware_support, install_firmware

from extract_utils.fixups_blob import (
    BlobFixupCtx,
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


def fixup_camera_metastore(ctx, file, file_path, *args, **kwargs):
    fix_imx766_stream_table(file_path)


def fixup_halsensor_poweron(ctx, file, file_path, *args, **kwargs):
    drop_poweron_sleep(file_path)


def fixup_rxfilter_firmware(ctx, file, file_path, *args, **kwargs):
    if os.environ.get('MINDONE_PERSONAL_BUILD', 'true') != 'true':
        return
    install_firmware(file_path)
    print(firmware_support(Path(file_path).read_bytes()))


def fixup_speech_vol_audio_param(ctx, file, file_path, *args, **kwargs):
    """Back off the handset uplink gain in the device's own volume tables."""
    tune_speech_vol_audio_param(file_path)


blob_fixups: blob_fixups_user_type = {
    'system_ext/priv-app/ImsService/ImsService.apk': blob_fixup()
        .apktool_unpack('ims-patches')
        .patch_dir('ims-patches')
        .patch_dir('ims-a17-patches/ImsService')
        .apktool_pack()
        .stripzip(),
    'system_ext/framework/mediatek-ims-common.jar': blob_fixup()
        .apktool_patch('ims-a17-patches/mediatek-ims-common'),
    'system_ext/framework/mediatek-ims-extension-plugin.jar': blob_fixup()
        .apktool_patch('ims-a17-patches/mediatek-ims-extension-plugin'),
    (
        'vendor/firmware/WIFI_RAM_CODE_soc2_2_1d_1.bin',
    ): blob_fixup()
        .call(fixup_rxfilter_firmware, need_tmp_dir=False),
    (
        'vendor/etc/audio_param/Speech_AudioParam.xml',
    ): blob_fixup()
        .call(fixup_speech_audio_param, need_tmp_dir=False),
    (
        'vendor/etc/audio_param/SpeechVol_AudioParam.xml',
    ): blob_fixup()
        .call(fixup_speech_vol_audio_param, need_tmp_dir=False),
    (
        'vendor/lib64/libmtkcam_metastore.so',
    ): blob_fixup()
        .call(fixup_camera_metastore, need_tmp_dir=False),
    (
        'vendor/lib64/libcam.halsensor.so',
    ): blob_fixup()
        .call(fixup_halsensor_poweron, need_tmp_dir=False),

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
        'vendor/lib64/libcam.feature_utils.so',
        'vendor/lib64/libcam.iopipe.so',
        'vendor/lib64/libcamalgo.warp.so',
        'vendor/lib64/libdip_postproc.so',
        'vendor/lib64/libfeature.face.so',
        'vendor/lib64/libfeature.stereo.provider.so',
        'vendor/lib64/libfeature.vsdof.hal.so',
        'vendor/lib64/libimageio_plat_drv.so',
        'vendor/lib64/libmtkcam_3rdparty.mtk.so',
        'vendor/lib64/libmtkcam_3rdparty.so',
        'vendor/lib64/libmtkcam_fdvt.so',
        'vendor/lib64/libmtkcam_hwnode.so',
        'vendor/lib64/libmtkcam_imgbuf.so',
        'vendor/lib64/libmtkcam_mfb.so',
        'vendor/lib64/libmtkcam_pipelinemodel_isp.so',
        'vendor/lib64/libmtkcam.featurepipe.capture.so',
        'vendor/lib64/libmtkcam.featurepipe.depthmap.so',
        'vendor/lib64/libmtkcam.featurepipe.streaming.so',
        'vendor/lib64/libmtkcam.featurepipe.vsdof_util.so',
    ): blob_fixup()
        .replace_needed('libdpframework.so', 'libdpfw_cam.so')
        .replace_needed('libgralloc_extra.so', 'libgrextra_cam.so'),
    'vendor/lib64/libdpfw_cam.so': blob_fixup()
        .replace_needed('libaiselector.so', 'libaisel_cam.so')
        .replace_needed('libgralloc_extra.so', 'libgrextra_cam.so'),
    'vendor/lib64/libgrextra_cam.so': blob_fixup()
        .replace_needed('libged.so', 'libged_cam.so'),
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
        'vendor/lib64/libaalservice.so',
    ): blob_fixup()
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so'),
    'vendor/lib64/librt_extamp_intf.so': blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-mtk.so'),
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
        'vendor/lib/hw/audio.r_submix.mt6789.so',
        'vendor/lib64/hw/audio.r_submix.mt6789.so',
    ): blob_fixup()
        .fix_soname(),
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
        .replace_needed('libpuresoftkeymasterdevice.so', 'libpuresoftkeymasterdevice_mtk.so')
        .add_needed('android.hardware.security.rkp-V1-ndk.so'),
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
}

def installed_paths(file):
    return {file.dst} | {
        alias if '/' in alias else str(Path(file.dst).parent / alias)
        for alias in file.args.get(FileArgs.SYMLINK, [])
    }


def checked_path(value):
    p = Path(value)
    if p.is_absolute() or '..' in p.parts or str(p) != value:
        raise ValueError(f'Unsafe blob path: {value}')
    return value


def donor_source(donor_root, value):
    candidate = donor_root / checked_path(value)
    for _ in range(40):
        if not candidate.is_symlink():
            if not candidate.resolve().is_relative_to(donor_root.resolve()):
                raise ValueError(f'Donor path escapes root: {value}')
            return candidate
        target = os.readlink(candidate)
        candidate = Path(os.path.normpath(
            donor_root / target.lstrip('/') if target.startswith('/')
            else candidate.parent / target))
        if not candidate.is_relative_to(donor_root):
            raise ValueError(f'Donor link escapes root: {value}')
    raise ValueError(f'Donor symlink loop: {value}')


def verify_donor(donor_root, file):
    src = donor_source(donor_root, file.src)
    if sha1(src.read_bytes()).hexdigest() != file.hash:
        raise ValueError(f'Donor pin mismatch: {src}')
    for alias in installed_paths(file) - {file.dst}:
        if not (donor_root / alias).is_symlink():
            raise ValueError(f'Donor alias is not a symlink: {alias}')
        if donor_source(donor_root, alias) != src:
            raise ValueError(f'Donor alias mismatch: {alias}')
    return src


class StockFile(ProprietaryFile):
    def parse(self):
        lines = Path(self.file_list_path).read_text().splitlines(keepends=True)
        full = FileList(check_elf=True)
        full.add_from_lines(lines)
        destinations = {file.dst for file in full.all_files}
        if retired_paths - destinations:
            raise ValueError(f'Unknown stock retirements: {sorted(retired_paths - destinations)}')
        occupied = set()
        kept = []
        for line in lines:
            if not line.strip() or line.lstrip().startswith('#'):
                kept.append(line)
                continue
            file = File(line)
            paths = installed_paths(file)
            if paths & occupied:
                raise ValueError(f'Duplicate stock install path: {file.dst}')
            occupied.update(paths)
            if file.dst in retired_paths:
                continue
            if paths & donor_paths:
                raise ValueError(f'Non-retired stock/donor collision: {sorted(paths & donor_paths)}')
            kept.append(line)
        self.file_list.add_from_lines(kept)
        if any(donor_roots.values()):
            self.file_list.files = full.files
            self.file_list.pinned_files = full.pinned_files
            self.file_list.partitions = full.partitions

    def write_to_file(self):
        raise ValueError('Updating stock pins requires MINDONE_DONOR_GROUPS to be empty')


class MindoneModule(ExtractUtilsModule):
    def parse(self, regenerate, section):
        if section is not None:
            raise ValueError('Donor groups require a complete extraction')
        result = super().parse(regenerate, section)
        for proprietary in self.proprietary_files:
            name = Path(proprietary.file_list_path).name
            if name in donor_lines:
                proprietary.file_list = FileList(check_elf=True)
                proprietary.file_list.add_from_lines(donor_lines[name])
        return result

    def process(self, *args, **kwargs):
        for donor, _, files in donors:
            if donor_roots[donor] is not None:
                for file in files:
                    verify_donor(donor_roots[donor], file)
        return super().process(*args, **kwargs)

    def stage_donor(self, donor, file, dst, source=None):
        dst.parent.mkdir(parents=True, exist_ok=True)
        expected = file.fixup_hash or file.hash
        with tempfile.TemporaryDirectory(dir=dst.parent) as temporary:
            staged = Path(temporary) / dst.name
            if donor_roots[donor] is not None:
                shutil.copy2(verify_donor(donor_roots[donor], file), staged)
            elif source is None or not source.copy_file_to_path(File(file.dst), str(staged)):
                raise RuntimeError(f'Extraction failed: {file.dst}')
            actual = sha1(staged.read_bytes()).hexdigest()
            if actual not in {file.hash, expected}:
                raise ValueError(f'Donor pin mismatch: {file.dst}')
            if actual != expected and FileArgs.FIX_SONAME in file.args:
                blob_fixup().fix_soname().run(
                    BlobFixupCtx(self.device_path), file, str(staged))
            if actual != expected and file.dst in DONOR_FIXUPS:
                DONOR_FIXUPS[file.dst].run(BlobFixupCtx(self.device_path), file, str(staged))
            if file.dst == 'vendor/firmware/WIFI_RAM_CODE_soc2_2_1d_1.bin':
                fixup_rxfilter_firmware(None, file, str(staged))
            if sha1(staged.read_bytes()).hexdigest() != expected:
                raise ValueError(f'Donor output pin mismatch: {dst}')
            os.replace(staged, dst)

    def process_file(self, file, source, backup_source, vendor_path,
                     is_firmware, kang, allow_prohibited_files=False):
        for donor in DONORS:
            if Path(vendor_path).is_relative_to(Path(self.vendor_path) / f'donor-{donor}'):
                self.stage_donor(donor, file, Path(vendor_path) / file.dst, source)
                return True
        ok = super().process_file(file, source, backup_source, vendor_path,
                                  is_firmware, kang, allow_prohibited_files)
        if not ok:
            raise RuntimeError(f'Extraction failed: {file.dst}')
        return ok

    def write_makefiles(self, legacy, extract_factory):
        for file in stock.file_list.all_files:
            if not (Path(self.vendor_path) / 'proprietary' / file.dst).is_file():
                raise RuntimeError(f'Re-extract stock blob: {file.dst}')
        for donor, group, files in donors:
            for file in files:
                dst = Path(self.vendor_path) / f'donor-{donor}' / group / file.dst
                expected = file.fixup_hash or file.hash
                if dst.is_file() and sha1(dst.read_bytes()).hexdigest() == expected:
                    continue
                if donor_roots[donor] is None:
                    raise RuntimeError(f'Re-extract donor blob or set {DONORS[donor][0]}: {file.dst}')
                self.stage_donor(donor, file, dst)
        super().write_makefiles(legacy, extract_factory)


DONOR_FIXUPS = {
    path: blob_fixup().replace_needed('android.hardware.graphics.common-V6-ndk.so',
                                      'android.hardware.graphics.common-V7-ndk.so')
    for path in (
        'vendor/bin/hw/mt6789/android.hardware.graphics.allocator-V2-service-mediatek.mt6789',
        'vendor/lib64/hw/hwcomposer.mtk_common.so',
        'vendor/lib64/hw/libsimplehwc.so',
        'vendor/lib64/hw/mt6789/android.hardware.graphics.allocator-V2-mediatek.so',
        'vendor/lib64/hw/mt6789/mapper.mediatek.so',
        'vendor/lib64/libgpud.so',
        'vendor/lib/libgpud.so',
        'vendor/lib64/vendor.mediatek.hardware.pq_aidl-V7-ndk.so',
        'vendor/lib/vendor.mediatek.hardware.pq_aidl-V7-ndk.so',
    )
}
for path in (
    'vendor/lib64/hw/hwcomposer.mtk_common.so',
    'vendor/lib64/libpqxmlflagparser.so',
    'vendor/lib64/libpqxmlparser.so',
    'vendor/lib64/libsilkybrightnesscore.so',
):
    DONOR_FIXUPS.setdefault(path, blob_fixup()).replace_needed('libtinyxml2.so', 'libtinyxml2-mtk.so')
DONOR_FIXUPS['vendor/lib64/mt6789/libmtkcam_grallocutils.so'] = (
    blob_fixup()
    .replace_needed('android.hardware.graphics.common-V5-ndk.so', 'android.hardware.graphics.common-V7-ndk.so')
    .replace_needed('libgralloc_extra.so', 'libgrextra_cam.so')
)

donor_paths = set()
donor_entries = {}
donor_lines = {}
retired_paths = set()
donors = []
DONORS = {
    'a075f': ('MINDONE_DONOR_A075F_ROOT', 'MINDONE_DONOR_GROUPS', 'graphics,radio,video,drm,connsys'),
    'x6853': ('MINDONE_DONOR_X6853_ROOT', 'MINDONE_DONOR_X6853_GROUPS', 'ims'),
    'lenovo-tb351fu': ('MINDONE_DONOR_LENOVO_TB351FU_ROOT',
                     'MINDONE_DONOR_LENOVO_TB351FU_GROUPS', 'video'),
}
donor_roots = {}
donor_groups = []
for donor, (root_variable, groups_variable, default) in DONORS.items():
    root = os.environ.get(root_variable)
    donor_roots[donor] = Path(root).expanduser().absolute() if root else None
    groups = [group.strip() for group in (os.environ.get(groups_variable) or default).split(',')]
    groups = [group for group in groups if group]
    if len(groups) != len(set(groups)):
        raise ValueError(f'Duplicate donor group in {groups_variable}')
    donor_groups.extend((donor, group) for group in sorted(groups))
if {group for donor, group in donor_groups if donor == 'a075f'} != {'graphics', 'radio', 'video', 'drm', 'connsys'}:
    raise ValueError('The paired ROM requires MINDONE_DONOR_GROUPS=graphics,radio,video,drm,connsys')
if {group for donor, group in donor_groups if donor == 'lenovo-tb351fu'} != {'video'}:
    raise ValueError('The paired ROM requires MINDONE_DONOR_LENOVO_TB351FU_GROUPS=video')
if {group for donor, group in donor_groups if donor == 'x6853'} != {'ims'}:
    raise ValueError('The 64-bit-only ROM requires MINDONE_DONOR_X6853_GROUPS=ims')
for donor, group in donor_groups:
    base = Path(__file__).parent / f'proprietary-files-donor-{donor}-{group}'
    parsed = []
    selected_lines = []
    group_paths = set()
    for line in base.with_suffix('.txt').read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        file = File(line)
        paths = installed_paths(file)
        for value in paths | {file.src}:
            checked_path(value)
        if group_paths & paths:
            raise ValueError(f'Duplicate donor group path: {file.dst}')
        group_paths.update(paths)
        if donor_paths & paths:
            if all(donor_entries.get(path) == line for path in paths):
                continue
            raise ValueError(f'Duplicate donor install path: {file.dst}')
        donor_paths.update(paths)
        donor_entries.update({path: line for path in paths})
        if not file.hash or not re.fullmatch('[0-9a-f]{40}', file.hash):
            raise ValueError(f'Missing SHA-1 pin: {line}')
        if file.args.keys() - {FileArgs.SYMLINK, FileArgs.FIX_SONAME,
                               FileArgs.MODULE, FileArgs.MODULE_SUFFIX}:
            raise ValueError(f'Unreviewed donor flags: {line}')
        if file.fixup_hash and not re.fullmatch('[0-9a-f]{40}', file.fixup_hash):
            raise ValueError(f'Invalid post-fixup pin: {line}')
        fixed = FileArgs.FIX_SONAME in file.args or file.dst in DONOR_FIXUPS
        if fixed and not file.fixup_hash:
            raise ValueError(f'Missing post-fixup pin: {line}')
        if file.fixup_hash and not fixed:
            raise ValueError(f'Unexpected post-fixup pin: {line}')
        parsed.append(file)
        selected_lines.append(line)
    group_retirements = set()
    for line in base.with_suffix('.retire.txt').read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        retirement = File(line.strip())
        checked_path(retirement.src)
        value = checked_path(retirement.dst)
        if value in group_retirements or value in retired_paths:
            raise ValueError(f'Duplicate stock retirement: {value}')
        group_retirements.add(value)
    retired_paths.update(group_retirements)
    donors.append((donor, group, parsed))
    donor_lines[base.with_suffix('.txt').name] = selected_lines

module_type = MindoneModule if donors else ExtractUtilsModule
module = module_type(
    'mindone',
    'ikko',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
    add_firmware_proprietary_file=True,
    skip_main_proprietary_file=bool(donors),
)

if donors:
    stock = StockFile(str(Path(__file__).parent / 'proprietary-files.txt'))
    module.proprietary_files.insert(0, stock)
    for donor, group, _ in donors:
        module.add_proprietary_file(
            f'proprietary-files-donor-{donor}-{group}.txt',
            vendor_rel_sub_path=f'donor-{donor}/{group}',
        )

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
