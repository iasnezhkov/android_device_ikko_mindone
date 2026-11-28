# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

import argparse
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


ENUM_CONTROLS = {
    "DAC In Mux": {"Normal Path", "Sgen"},
    "ADDA_UL_Mux": {"MTKAIF", "AP_DMIC"},
    "LOL Mux": {"Open", "Mute", "Playback", "Test Mode"},
    "RCV Mux": {"Open", "Mute", "Voice Playback", "Test Mode", "Media Playback"},
    "HPL Mux": {
        "Open", "LoudSPK Playback", "Audio Playback", "Test Mode",
        "HP Impedance", "Loud DualSPK Playback",
    },
    "HPR Mux": {
        "Open", "LoudSPK Playback", "Audio Playback", "Test Mode",
        "HP Impedance", "Loud DualSPK Playback",
    },
    "ADC L Mux": {"Idle", "AIN0", "Left Preamplifier", "Idle_1"},
    "ADC R Mux": {"Idle", "AIN0", "Right Preamplifier", "Idle_1"},
    "PGA L Mux": {"None", "AIN0", "AIN1", "AIN2"},
    "PGA R Mux": {"None", "AIN0", "AIN1", "AIN2"},
    "MTKAIF_DMIC": {"Off", "On"},
    "BTCVSD Band": {"NB", "WB"},
}

INT_CONTROLS = {
    "Headphone Volume": (0, 18),
    "Lineout Volume": (0, 18),
    "Handset Volume": (0, 18),
    "PGA Volume": (0, 4),
    "Ext_Speaker_Amp Switch": (0, 1),
    "BTCVSD Tx Mute Switch": (0, 1),
    "Speech_SHM_USIP": (0, 1),
    "Speech_SHM_Widx": (0, 0xFFFFFFFF),
    "ADDA_DL_CH1 DL1_CH1": (0, 1),
    "ADDA_DL_CH2 DL1_CH2": (0, 1),
    "ADDA_DL_CH1 DL2_CH1": (0, 1),
    "ADDA_DL_CH2 DL2_CH2": (0, 1),
    "ADDA_DL_CH1 DL3_CH1": (0, 1),
    "ADDA_DL_CH2 DL3_CH2": (0, 1),
    "ADDA_DL_CH1 PCM_2_CAP_CH1": (0, 1),
    "ADDA_DL_CH2 PCM_2_CAP_CH1": (0, 1),
    "PCM_2_PB_CH1 ADDA_UL_CH1": (0, 1),
    "PCM_2_PB_CH2 ADDA_UL_CH2": (0, 1),
    "PCM_2_PB_CH4 DL1_CH1": (0, 1),
    "PCM_2_PB_CH5 DL1_CH2": (0, 1),
    "UL_MONO_1_CH1 PCM_2_CAP_CH1": (0, 1),
    "UL1_CH1 ADDA_UL_CH1": (0, 1),
    "UL1_CH2 ADDA_UL_CH2": (0, 1),
}

FORBIDDEN_LEGACY_NAMES = {
    "Headset Volume",
    "PGA1 Volume",
    "PGA2 Volume",
    "Media Playback",
}

REQUIRED_POLICY_OUTPUTS = {
    "AUDIO_DEVICE_OUT_EARPIECE",
    "AUDIO_DEVICE_OUT_SPEAKER",
    "AUDIO_DEVICE_OUT_WIRED_HEADSET",
    "AUDIO_DEVICE_OUT_WIRED_HEADPHONE",
    "AUDIO_DEVICE_OUT_BLUETOOTH_SCO",
    "AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET",
    "AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT",
    "AUDIO_DEVICE_OUT_TELEPHONY_TX",
}

REQUIRED_POLICY_INPUTS = {
    "AUDIO_DEVICE_IN_BUILTIN_MIC",
    "AUDIO_DEVICE_IN_WIRED_HEADSET",
    "AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET",
    "AUDIO_DEVICE_IN_VOICE_CALL",
}

ALLOWED_IMPLICIT_POLICY_DEVICES = {
    "AUDIO_DEVICE_IN_BACK_MIC",
}

PLAYBACK_SOURCES = {"primary output", "deep_buffer", "fast", "Voice Call In"}


def extract_function(source, signature):
    if source.count(signature) != 1:
        raise ValueError(f"Expected one definition for {signature}")
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def validate_kcontrol_value(name, value, location, errors):
    if name in FORBIDDEN_LEGACY_NAMES or value in FORBIDDEN_LEGACY_NAMES:
        errors.append(f"{location}: deprecated control or enum token {name!r}={value!r}")
        return
    if name in ENUM_CONTROLS:
        if value not in ENUM_CONTROLS[name]:
            errors.append(
                f"{location}: invalid enum value {value!r} for {name!r}; "
                f"expected one of {sorted(ENUM_CONTROLS[name])}"
            )
        return
    if name in INT_CONTROLS:
        lo, hi = INT_CONTROLS[name]
        try:
            parsed = int(value, 0)
        except ValueError:
            errors.append(f"{location}: non-integer value {value!r} for {name!r}")
            return
        if parsed < lo or parsed > hi:
            errors.append(
                f"{location}: value {parsed} out of range [{lo}, {hi}] for {name!r}"
            )
        return
    errors.append(f"{location}: unknown kcontrol {name!r} not in MT6366/MT6789/BTCVSD contract")


def check_mixer_and_hal_controls(device_root, errors):
    mixer_path = device_root / "audio-hf/mixer_paths.xml"
    hal_path = device_root / "audio-hf/audio_hw.c"
    root = ET.parse(mixer_path).getroot()
    hal_src = hal_path.read_text(encoding="utf-8")

    defined_paths = set()
    xml_controls = set()
    for child in root:
        if child.tag == "ctl":
            name = child.attrib.get("name", "")
            value = child.attrib.get("value", "")
            xml_controls.add(name)
            validate_kcontrol_value(name, value, "mixer_paths.xml:<root>", errors)
        elif child.tag == "path":
            pname = child.attrib.get("name", "")
            if not pname:
                errors.append("mixer_paths.xml: unnamed <path>")
                continue
            if pname in defined_paths:
                errors.append(f"mixer_paths.xml: duplicate <path name={pname!r}>")
            defined_paths.add(pname)
            for ctl in child:
                if ctl.tag != "ctl":
                    errors.append(f"mixer_paths.xml:{pname}: unexpected element <{ctl.tag}>")
                    continue
                name = ctl.attrib.get("name", "")
                value = ctl.attrib.get("value", "")
                xml_controls.add(name)
                validate_kcontrol_value(name, value, f"mixer_paths.xml:{pname}", errors)

    all_contract = set(ENUM_CONTROLS) | set(INT_CONTROLS)
    hal_controls = set()

    for match in re.finditer(r'mixer_get_ctl_by_name\([^,]+,\s*"([^"]+)"\)', hal_src):
        hal_controls.add(match.group(1))
    for match in re.finditer(r'^#define\s+\w+_CTL\s+"([^"]+)"', hal_src, re.M):
        hal_controls.add(match.group(1))
    vol_fn = extract_function(hal_src, "static unsigned int output_volume_control_names(")
    for match in re.finditer(r'names\[\d+\]\s*=\s*"([^"]+)"', vol_fn):
        hal_controls.add(match.group(1))

    for ctl in sorted(hal_controls):
        if ctl in FORBIDDEN_LEGACY_NAMES:
            errors.append(f"audio_hw.c: deprecated kcontrol name {ctl!r}")
        elif ctl not in all_contract:
            errors.append(f"audio_hw.c: unknown kcontrol {ctl!r} not in MT6366/MT6789/BTCVSD contract")

    bt_fn = extract_function(hal_src, "static void bt_sco_configure(")
    for token in re.findall(r'"(WB|NB)"', bt_fn):
        validate_kcontrol_value("BTCVSD Band", token, "audio_hw.c:bt_sco_configure", errors)

    hal_paths = set()
    for sig in (
        "static const char *output_path_name(",
        "static const char *output_path_off_name(",
        "static const char *dl_crossbar_path_name(",
        "static const char *dl_crossbar_off_path_name(",
        "static const char *input_path_name(",
        "static const char *input_path_off_name(",
    ):
        fn = extract_function(hal_src, sig)
        for match in re.finditer(r'return\s+"([^"]+)"', fn):
            hal_paths.add(match.group(1))
    for match in re.finditer(r'audio_route_apply_path\([^,]+,\s*([^)]+)\)', hal_src):
        for literal in re.findall(r'"([^"]+)"', match.group(1)):
            hal_paths.add(literal)

    for pname in sorted(hal_paths):
        if pname not in defined_paths:
            errors.append(f"audio_hw.c: mixer path {pname!r} missing from mixer_paths.xml")

    return xml_controls | hal_controls, defined_paths


def check_kernel_drivers(kernel_dir, used_controls, errors):
    sources = {}
    for path in sorted(kernel_dir.rglob("*.[ch]")):
        sources[path] = path.read_text(encoding="utf-8", errors="ignore")
    combined = "\n".join(sources.values())

    for ctl in sorted(used_controls):
        if ctl == "Ext_Speaker_Amp Switch":
            present = '"Ext_Speaker_Amp"' in combined and "SOC_DAPM_PIN_SWITCH" in combined
        elif " " in ctl and all("_" in part for part in ctl.split(" ", 1)):
            widget, switch = ctl.split(" ", 1)
            present = f'"{widget}"' in combined and f'"{switch}"' in combined
        else:
            present = f'"{ctl}"' in combined
        if not present:
            errors.append(f"kernel contract mismatch: {ctl!r} not found under {kernel_dir}")


def check_policy_consistency(device_root, errors):
    policy_path = device_root / "configs/audio/audio_policy_configuration.xml"
    hal_path = device_root / "audio-hf/audio_hw.c"
    root = ET.parse(policy_path).getroot()
    hal_src = hal_path.read_text(encoding="utf-8")

    primary = None
    for module in root.findall("./modules/module"):
        if module.attrib.get("name") == "primary":
            primary = module
            break
    if primary is None:
        errors.append("audio_policy_configuration.xml: missing <module name='primary'>")
        return

    mix_ports = {}
    for mp in primary.findall("./mixPorts/mixPort"):
        name = mp.attrib.get("name", "")
        role = mp.attrib.get("role", "")
        mix_ports[name] = role
        for prof in mp.findall("./profile"):
            if prof.attrib.get("format") != "AUDIO_FORMAT_PCM_16_BIT":
                errors.append(f"audio_policy: mixPort {name!r} unsupported format {prof.attrib.get('format')!r}")
            if prof.attrib.get("samplingRates") != "48000":
                errors.append(f"audio_policy: mixPort {name!r} unsupported rate {prof.attrib.get('samplingRates')!r}")
            expected_mask = "AUDIO_CHANNEL_OUT_STEREO" if role == "source" else "AUDIO_CHANNEL_IN_MONO"
            if prof.attrib.get("channelMasks") != expected_mask:
                errors.append(
                    f"audio_policy: mixPort {name!r} channel mask {prof.attrib.get('channelMasks')!r} "
                    f"!= {expected_mask!r}"
                )

    device_ports = {}
    device_types = {}
    for dp in primary.findall("./devicePorts/devicePort"):
        tag = dp.attrib.get("tagName", "")
        dtype = dp.attrib.get("type", "")
        role = dp.attrib.get("role", "")
        device_ports[tag] = (dtype, role)
        device_types[dtype] = (tag, role)

    for item in primary.findall("./attachedDevices/item"):
        name = (item.text or "").strip()
        if name not in device_ports:
            errors.append(f"audio_policy: attachedDevice {name!r} not in devicePorts")

    default_out = primary.find("./defaultOutputDevice")
    if default_out is None or (default_out.text or "").strip() not in device_ports:
        errors.append("audio_policy: defaultOutputDevice missing or undefined in devicePorts")

    routes_by_sink = {}
    all_ports = {name: role for name, role in mix_ports.items()}
    for tag, (_, role) in device_ports.items():
        all_ports[tag] = role

    for route in primary.findall("./routes/route"):
        sink = route.attrib.get("sink", "")
        sources = [s.strip() for s in route.attrib.get("sources", "").split(",") if s.strip()]
        if sink not in all_ports:
            errors.append(f"audio_policy: route sink {sink!r} undefined")
            continue
        if all_ports[sink] != "sink":
            errors.append(f"audio_policy: route sink {sink!r} does not have role='sink'")
        for src in sources:
            if src not in all_ports:
                errors.append(f"audio_policy: route source {src!r} for sink {sink!r} undefined")
            elif all_ports[src] != "source":
                errors.append(f"audio_policy: route source {src!r} for sink {sink!r} does not have role='source'")
        routes_by_sink.setdefault(sink, set()).update(sources)

    hal_devices = {
        tok for tok in re.findall(r"\bAUDIO_DEVICE_(?:OUT|IN)_[A-Z0-9_]+\b", hal_src)
    }

    for dtype, (tag, _) in sorted(device_types.items()):
        if dtype not in hal_devices and dtype not in ALLOWED_IMPLICIT_POLICY_DEVICES:
            errors.append(f"audio_policy: devicePort {tag!r} ({dtype}) is not handled in audio_hw.c")

    for req in sorted(REQUIRED_POLICY_OUTPUTS):
        if req not in hal_devices:
            errors.append(f"audio_hw.c: missing expected output device handling for {req}")
        if req not in device_types:
            errors.append(f"audio_policy: missing output devicePort for {req}")
            continue
        tag, role = device_types[req]
        if role != "sink":
            errors.append(f"audio_policy: output devicePort {tag!r} ({req}) has role={role!r}")
        if req != "AUDIO_DEVICE_OUT_TELEPHONY_TX":
            missing_src = PLAYBACK_SOURCES - routes_by_sink.get(tag, set())
            if missing_src:
                errors.append(
                    f"audio_policy: route to {tag!r} ({req}) missing sources {sorted(missing_src)}"
                )

    for req in sorted(REQUIRED_POLICY_INPUTS):
        if req not in hal_devices:
            errors.append(f"audio_hw.c: missing expected input device handling for {req}")
        if req not in device_types:
            errors.append(f"audio_policy: missing input devicePort for {req}")
            continue
        tag, role = device_types[req]
        if role != "source":
            errors.append(f"audio_policy: input devicePort {tag!r} ({req}) has role={role!r}")
        if req == "AUDIO_DEVICE_IN_VOICE_CALL":
            if tag not in routes_by_sink.get("voice tx", set()):
                errors.append(f"audio_policy: voice tx route missing source {tag!r}")
        else:
            for sink_name in ("primary input", "fast input", "Telephony Tx"):
                if tag not in routes_by_sink.get(sink_name, set()):
                    errors.append(
                        f"audio_policy: route to {sink_name!r} missing input source {tag!r} ({req})"
                    )


def parse_bp_modules(bp_text):
    modules = {}
    pattern = re.compile(r'([a-zA-Z0-9_]+)\s*\{([^{}]*(?:\{[^{}]*\}[^{}]*)*)\}', re.S)
    for match in pattern.finditer(bp_text):
        kind, body = match.group(1), match.group(2)
        name_m = re.search(r'name\s*:\s*"([^"]+)"', body)
        if name_m:
            modules[name_m.group(1)] = (kind, body)
    return modules


def check_bridge_and_rust_linkage(device_root, errors):
    bridge_dir = device_root / "bridges/audio"
    audio_hf_bp_path = device_root / "audio-hf/Android.bp"
    bridge_bp_path = bridge_dir / "Android.bp"

    backend_cpp = (bridge_dir / "Backend.cpp").read_text(encoding="utf-8")
    if "mindone_audio_set_sco(" not in backend_cpp:
        errors.append("bridges/audio/Backend.cpp: missing production call to mindone_audio_set_sco")

    module_cpp = (bridge_dir / "ModuleMindone.cpp").read_text(encoding="utf-8")
    if "ModuleMindone::getBluetooth(" not in module_cpp or "BluetoothMindone" not in module_cpp:
        errors.append("bridges/audio/ModuleMindone.cpp: must override getBluetooth with BluetoothMindone")

    bt_cpp_path = bridge_dir / "BluetoothMindone.cpp"
    if not bt_cpp_path.is_file():
        errors.append("bridges/audio/BluetoothMindone.cpp: missing IBluetooth implementation")
    else:
        bt_cpp = bt_cpp_path.read_text(encoding="utf-8")
        for token in (
            "BluetoothMindone::setScoConfig(",
            "ScoConfig::Mode::SCO_WB",
            "ScoConfig::Mode::SCO",
            "MINDONE_AUDIO_SCO_WB",
            "MINDONE_AUDIO_SCO_NB",
            "setSco(",
        ):
            if token not in bt_cpp:
                errors.append(f"bridges/audio/BluetoothMindone.cpp: missing required SCO wiring {token!r}")

    bridge_bp = bridge_bp_path.read_text(encoding="utf-8")
    audio_hf_bp = audio_hf_bp_path.read_text(encoding="utf-8")
    bridge_mods = parse_bp_modules(bridge_bp)
    audio_mods = parse_bp_modules(audio_hf_bp)

    svc = bridge_mods.get("android.hardware.audio.service.mindone")
    if svc is None:
        errors.append("bridges/audio/Android.bp: missing android.hardware.audio.service.mindone")
        return
    _, svc_body = svc
    if '"BluetoothMindone.cpp"' not in svc_body:
        errors.append("bridges/audio/Android.bp: BluetoothMindone.cpp not listed in srcs")

    linked_backends = [
        name for name in ("libmindone_audio_backend", "libaudio.mindone.rust.impl")
        if f'"{name}"' in svc_body
    ]
    if not linked_backends:
        errors.append("bridges/audio/Android.bp: service does not link any audio-hf backend target")
        return

    for backend_name in linked_backends:
        mod = audio_mods.get(backend_name)
        if mod is None:
            errors.append(f"audio-hf/Android.bp: linked backend {backend_name!r} is not defined")
            continue
        _, body = mod
        if "-DMINDONE_AUDIO_RUST_ROUTES" not in body:
            errors.append(
                f"audio-hf/Android.bp: production backend {backend_name!r} lacks -DMINDONE_AUDIO_RUST_ROUTES"
            )
        if '"libmindone_audio_routes_ffi"' not in body and '"libmindone_audio_routes_ffi"' not in svc_body:
            errors.append(
                f"audio-hf/Android.bp: production backend {backend_name!r} does not link libmindone_audio_routes_ffi"
            )

    for dead_name in ("libmindone_audio_backend", "libaudio.mindone.rust.impl"):
        if dead_name in audio_mods and dead_name not in linked_backends:
            errors.append(
                f"audio-hf/Android.bp: unlinked duplicate backend target {dead_name!r} remains defined"
            )


def main():
    parser = argparse.ArgumentParser(
        description="Cross-check audio mixer kcontrols, policy ports/routes, SCO bridge wiring, and Rust linkage"
    )
    parser.add_argument(
        "--device-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    parser.add_argument(
        "--kernel-dir",
        type=Path,
        default=None,
    )
    args = parser.parse_args()

    errors = []
    used_controls, defined_paths = check_mixer_and_hal_controls(args.device_root, errors)
    if args.kernel_dir is not None:
        check_kernel_drivers(args.kernel_dir, used_controls, errors)
    check_policy_consistency(args.device_root, errors)
    check_bridge_and_rust_linkage(args.device_root, errors)

    if errors:
        for err in errors:
            print(f"FAIL: {err}", file=sys.stderr)
        sys.exit(1)

    print(
        f"PASS mixer/policy/bridge cross-check: {len(used_controls)} kcontrols, "
        f"{len(defined_paths)} mixer paths, policy devicePorts/routes, "
        f"mindone_audio_set_sco wiring, and Rust route-planner linkage"
    )


if __name__ == "__main__":
    main()
