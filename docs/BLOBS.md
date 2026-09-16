# Proprietary files

## How they get here

`proprietary-files.txt` is a **list of 1308 paths**, not the files. Nothing proprietary is in
this repository. You extract them from your own device:

```sh
./extract-files.py --adb      # device connected, bootloader unlocked, adb root available
```

The script pulls each listed path off the device and writes them under
`vendor/ikko/mindone/`, then `setup-makefiles.py` generates the makefiles that ship them.

This is the standard LineageOS model and the only lawful one: the files belong to MediaTek and
iKKO, so each user takes them from hardware they own.

## What is deliberately not taken

19 entries in the list are commented out, each with the reason next to it. The largest group
is the stock MediaTek audio HAL: this device tree ships its own (`audio-hf/`), selected by
`ro.hardware.audio.primary=mindone`, so pulling the stock one in would leave two implementations
fighting over the same property.

Read the comments before re-enabling anything. A line commented out here usually means something
in the tree replaces it, not that it was forgotten.

## Versions

Blobs come from the device's own factory firmware. Mixing them with blobs from a different
firmware version is the most common cause of a build that flashes and then fails in ways that
look unrelated — camera that never opens, Wi-Fi that never associates.

If you need to know which firmware a blob set came from, record it yourself: this repository
deliberately stores no device identifiers, so it cannot tell you.
