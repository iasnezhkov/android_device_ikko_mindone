# sepolicy/vendor - rules on top of `device/mediatek/sepolicy_vndr` (MT6789 baseline)

Filled in on 06.09 from the `avc: denied` messages of the first permissive boots of this ROM:
genfs_contexts (vibrator, eta6965), mtk_hal_c2.te, vendor_init.te, file.te. For repackaging, see
`../repack-extra.cil` (same intent, in CIL).

Rules here are written only from denials seen on this device. Do not copy another device's `.te`
files in: they are tuned for that device's denials, not this one's.
