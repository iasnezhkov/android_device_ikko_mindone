# Contributing

This is a small, independent device tree. Reports and patches are welcome; please make them easy
to act on.

## The one rule that matters most

🔴 **Never commit proprietary files.** `proprietary-files.txt` lists ~960 vendor blobs this
build needs. They are *not* in this repository and must never be: they belong to MediaTek and
iKKO, and every user extracts them from their own device with `extract-files.py`. CI rejects any
push that adds a file named in that list, and any ELF, archive, `.apk` or `.so`.

If a patch needs a new blob, add its path to `proprietary-files.txt` — not the file.

## Before reporting a problem

Say which of these you have, because the answer changes completely:

- the **same device** (iKKO MindOne), or
- a **different MT6789/MT8781 device** — the HALs and most of `device.mk` should transfer;
  `BoardConfig.mk` partition geometry, the fstab and the panel will not.

Then include:

| | |
|---|---|
| Android/LineageOS version | what you are building, e.g. lineage-23.2 |
| What you ran | the exact commands, not a description of them |
| What happened | the actual output, trimmed but not paraphrased |
| Kernel | this tree does not build the kernel — say which kernel build you paired it with |

For a device that boots to a black screen with adb alive, `dmesg` and the output of
`logcat -b all -d` say more than a description does.

## Patches

- One change per commit, with a message that says **why** — the diff already says what.
- English only, in code and in commit messages.
- Where a value is device-specific, say in a comment how it was obtained. A number in a
  `BoardConfig.mk` that nobody can trace is a number nobody can fix.
- Do not add a value taken from another device's tree without checking it against this hardware.
  That mistake has already been made here twice, and both times it produced an image that built
  cleanly and did not boot.

## What this project will not take

- Vendor blobs, firmware, or prebuilt binaries of any kind.
- Attestation, SafetyNet or integrity workarounds.
- Code copied from another tree without a note saying where it came from and under what license.
