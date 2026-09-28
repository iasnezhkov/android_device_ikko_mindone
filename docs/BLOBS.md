# Proprietary files

## How they get here

`proprietary-files.txt` is a **list of about 960 paths**, not the files. Nothing proprietary is in
this repository. You extract them from your own device:

```sh
./extract-files.py --adb      # device connected, bootloader unlocked, adb root available
```

The script pulls each listed path off the device and writes them under
`vendor/ikko/mindone/`, then `setup-makefiles.py` generates the makefiles that ship them.

This is the standard LineageOS model and the only lawful one: the files belong to MediaTek and
iKKO, so each user takes them from hardware they own.

While it is at it, extraction also patches three stock binaries in place, byte for byte, against
a checked stock SHA-256 (`camera_metastore_fixup.py`, `camerahalserver_instance_fixup.py`,
`halsensor_poweron_fixup.py`): a fix to the IMX766 camera stream table, a fix that drops a fixed
300 ms sleep from the sensor power-on path, and — only when `MINDONE_CAMERA_PROXY=true` — a
rename of the stock camera provider's declared HIDL instance. Each one refuses to touch a file
whose hash it does not recognize, rather than patch blindly.

## What is deliberately not taken

A number of entries in the list are commented out, each with the reason next to it. The largest
group is the stock MediaTek audio HAL: this device tree ships its own (`audio-hf/`), selected by
`ro.hardware.audio.primary=mindone`, so pulling the stock one in would leave two implementations
fighting over the same property. Video codec and OMX blobs are excluded the same way now that
hardware decode/encode goes through the open V4L2 Codec2 HAL by default (see
[docs/OWN-COMPONENTS.md](OWN-COMPONENTS.md)).

Read the comments before re-enabling anything. A line commented out here usually means something
in the tree replaces it, not that it was forgotten.

## Versions

Blobs come from the device's own factory firmware. Mixing them with blobs from a different
firmware version is the most common cause of a build that flashes and then fails in ways that
look unrelated — camera that never opens, Wi-Fi that never associates.

If you need to know which firmware a blob set came from, record it yourself: this repository
deliberately stores no device identifiers, so it cannot tell you.
