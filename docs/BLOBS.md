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

## Wi-Fi RXFILTER firmware

The kernel RXFILTER implementation requires matching Wi-Fi firmware. The replacement sources,
placement builder and instruction-simulator checks are in `tools/wifi/` and `tools/rigs/` in
this device repository. No vendor firmware or compiled replacement is distributed here.

Extraction from a device already running the reviewed firmware needs no additional artifact:
`rxfilter_firmware.py` checks its complete SHA-256, size, CRC and the source hashes pinned in
`tools/wifi/rxfilter-release.json`. A stock or earlier firmware requires a locally built artifact
and its generated `.json` receipt through `MINDONE_RXFILTER_ARTIFACT`. Unknown inputs and stale
sources are rejected. An explicitly supplied artifact is always checked, even on repeated extraction.

The stock input SHA-256 is
`453912e5f79ba313986a8a6047a22f4d8033300f6db728f281a7f1c2ee92c37b`.
The reviewed output is 698052 bytes with SHA-256
`bfb11a2b31fdd22aadb603b186e9ae0ebc98a476a7f7adf0f4a572d520abbfec`.
Other compiler output is not implicitly accepted as a compatible firmware release.

Reproduction uses GCC 14.3.0 configured for `nds32le-elf` (C only, without target headers),
GNU binutils 2.43.1 for the same target, and the NDS32 simulator from `andestech/gdb` commit
`4327fa948f903c0705e0d87eed4cb182e24a6b6c`. Configure GCC with `--with-gnu-as --with-gnu-ld`
and `--with-as`/`--with-ld` pointing at that binutils build. Build GCC with `make all-gcc`;
the scripts take the build-tree `gcc/xgcc`, not an installed compiler wrapper. The binutils
directory must contain `gas/as-new`, `ld/ld-new` and `binutils/{objdump,objcopy,nm-new}`.
Configure the simulator with `--target=nds32le-elf --enable-sim --disable-gdb`
and build `all-sim`; its executable is `sim/nds32/run`. Host build dependencies and flags
depend on the build machine. The firmware builder itself pins the reduced-register NDS32 ABI.

Run these commands from this device repository, using absolute paths for the local input,
toolchain and a new output directory. Obtain the stock input from your own stock blob backup
or firmware extraction before the fixup; an already patched image is not a stock build input.

```sh
FW_INPUT=/path/to/stock/WIFI_RAM_CODE_soc2_2_1d_1.bin
NDS_GCC=/path/to/gcc-build/gcc/xgcc
NDS_BINUTILS=/path/to/binutils-build
NDS_SIM=/path/to/simulator-build/sim/nds32/run
RX_OUT=/path/to/new-rxfilter-output
mkdir -p "$RX_OUT"
python3 tools/wifi/rxfilter-build.py --firmware "$FW_INPUT" --gcc "$NDS_GCC" --binutils "$NDS_BINUTILS" --output "$RX_OUT/proof"
python3 tools/rigs/wifi-rxfilter-image-check.py --firmware "$FW_INPUT" --proof "$RX_OUT/proof" --gcc "$NDS_GCC" --binutils "$NDS_BINUTILS" --simulator "$NDS_SIM" --output "$RX_OUT/simulation"
python3 tools/wifi/rxfilter-patch.py --firmware "$FW_INPUT" --proof "$RX_OUT/proof" --composition-receipt "$RX_OUT/simulation/receipt.json" --output "$RX_OUT/WIFI_RAM_CODE_soc2_2_1d_1.bin"
export MINDONE_RXFILTER_ARTIFACT="$RX_OUT/WIFI_RAM_CODE_soc2_2_1d_1.bin"
python3 rxfilter_firmware.py --verify-only
./extract-files.py --adb
```

The patcher requires a passing instruction-composition receipt bound to the exact source and
linked code. Simulator checks establish software behavior; they do not measure battery life.
Keep this firmware paired with the kernel and device-tree RXFILTER implementation.

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
