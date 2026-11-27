# sepolicy/vendor - rules on top of `device/mediatek/sepolicy_vndr` (MT6789 baseline)

Device-specific policy additions for MindOne hardware and vendor services:
genfs_contexts (vibrator, charger, display, sensors), media/codec2, camera, audio,
modem/IMS netlink xperms, and vendor_init properties.

Rules here are scoped strictly to this board's services and hardware nodes. Do not copy
another device's `.te` files in without auditing every rule against MindOne.
