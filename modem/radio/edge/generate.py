# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from route_generate import emit_routes
from route_schema import ROUTES, DEFERRED

ROOT = Path(__file__).resolve().parent
DEFAULT_AIDL = Path(os.environ.get('ANDROID_BUILD_TOP', '.')) / 'hardware/interfaces/radio/aidl'
DOMAIN_VERSIONS = {'config': 4, 'data': 4, 'messaging': 4, 'modem': 4, 'network': 4, 'sim': 4, 'voice': 4, 'ims': 3}
DOMAINS = tuple(DOMAIN_VERSIONS)
RESPONSE_ALIASES = {('voice', 'hangup'): 'hangupConnectionResponse'}
SPDX = '/*\n * SPDX-License-Identifier: Apache-2.0\n * SPDX-FileCopyrightText: 2026 The LineageOS Project\n */\n\n'
PRIMITIVES = {'boolean': 'bool', 'byte': 'int8_t', 'int': 'int32_t', 'long': 'int64_t', 'float': 'float', 'double': 'double'}


def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def interface_path(aidl, domain, suffix=''):
    name = 'IRadio' + domain.title() + suffix
    return aidl / 'aidl_api' / f'android.hardware.radio.{domain}' / f'{DOMAIN_VERSIONS[domain]}/android/hardware/radio' / domain / (name + '.aidl')


def type_path(aidl, name):
    parts = name.split('.')
    domain = parts[3] if len(parts) > 4 and parts[:3] == ['android', 'hardware', 'radio'] else None
    version = DOMAIN_VERSIONS.get(domain, 4)
    for end in range(len(parts), 0, -1):
        package = '.'.join(parts[:end - 1])
        path = aidl / 'aidl_api' / package / str(version) / Path(*parts[:end]).with_suffix('.aidl')
        if path.is_file():
            return path
    raise ValueError(f'Missing frozen type: {name} V{version}')


def parse(path):
    text = clean(path.read_text())
    interface = re.search(r'\binterface\s+(\w+)\s*\{(.*)\}', text, re.S)
    if not interface:
        raise ValueError(f'Missing interface: {path}')
    methods = []
    for declaration in interface[2].split(';'):
        declaration = declaration.strip()
        if not declaration:
            continue
        match = re.fullmatch(r'(?:oneway\s+)?void\s+(\w+)\s*\((.*?)\)', declaration, re.S)
        if not match:
            raise ValueError(f'Unsupported declaration in {path}: {declaration}')
        args = []
        for raw in match[2].split(',') if match[2].strip() else []:
            nullable = '@nullable' in raw
            raw = re.sub(r'@nullable\b', '', raw).strip()
            raw = re.sub(r'^(?:in|out|inout)\s+', '', raw)
            arg = re.fullmatch(r'([\w.]+(?:\[\])?)\s+(\w+)', raw.strip())
            if not arg:
                raise ValueError(f'Unsupported argument: {raw}')
            args.append({'name': arg[2], 'type': arg[1], 'nullable': nullable})
        methods.append({'name': match[1], 'parameters': args})
    names = [method['name'] for method in methods]
    if len(names) != len(set(names)):
        raise ValueError(f'Duplicate methods in {path}')
    return methods


class Types:
    def __init__(self, aidl):
        self.aidl = aidl
        self.cache = {}

    def category(self, name):
        if name in self.cache:
            return self.cache[name]
        parts = name.split('.')
        path = type_path(self.aidl, name)
        text = clean(path.read_text())
        match = re.search(r'\b(enum|interface|parcelable|union)\s+' + re.escape(parts[-1]) + r'\b', text)
        if not match:
            raise ValueError(f'Unknown declaration: {path}')
        self.cache[name] = match[1]
        return match[1]

    def cpp(self, arg):
        name = arg['type']
        if name.endswith('[]'):
            element = {'type': name[:-2], 'nullable': False}
            value = 'uint8_t' if element['type'] == 'byte' else self.cpp(element)
            result = f'std::vector<{value}>'
        elif name in PRIMITIVES:
            result = PRIMITIVES[name]
        elif name == 'String':
            result = 'std::string'
        else:
            result = '::aidl::' + name.replace('.', '::')
            if self.category(name) == 'interface':
                result = f'std::shared_ptr<{result}>'
        if arg['nullable']:
            if not ('.' in name and not name.endswith('[]') and self.category(name) == 'interface'):
                result = f'std::optional<{result}>'
        return result

    def parameter(self, arg):
        value = self.cpp(arg)
        scalar = arg['type'] in PRIMITIVES or ('.' in arg['type'] and not arg['type'].endswith('[]') and self.category(arg['type']) == 'enum')
        return value if scalar and not arg['nullable'] else f'const {value}&'


def load(aidl):
    types = Types(aidl)
    inventory = {'aidl_versions': DOMAIN_VERSIONS, 'services': {}}
    for domain in DOMAINS:
        request_path = interface_path(aidl, domain)
        requests = parse(request_path)
        responses = {item['name']: item for item in parse(interface_path(aidl, domain, 'Response'))}
        for method in requests:
            serials = [arg for arg in method['parameters'] if arg['name'] == 'serial']
            method['serial'] = bool(serials)
            if serials:
                if len(serials) != 1 or serials[0]['type'] != 'int':
                    raise ValueError(f'Invalid serial in {domain}.{method["name"]}')
                response = responses.get(RESPONSE_ALIASES.get((domain, method['name']), method['name'] + 'Response'))
                if response is None or not response['parameters'] or response['parameters'][0]['type'] != 'android.hardware.radio.RadioResponseInfo':
                    raise ValueError(f'Missing matching response: {domain}.{method["name"]}')
                method['response'] = response
            elif method['name'] not in ('setResponseFunctions', 'responseAcknowledgement'):
                raise ValueError(f'Unknown lifecycle method: {domain}.{method["name"]}')
            for arg in method['parameters']:
                arg['cpp_type'] = types.cpp(arg)
                arg['cpp_parameter'] = types.parameter(arg)
        inventory['services'][domain] = {'aidl_version': DOMAIN_VERSIONS[domain], 'source': str(request_path.relative_to(aidl)), 'sha256': hashlib.sha256(request_path.read_bytes()).hexdigest(), 'method_count': len(requests), 'serial_method_count': sum(method['serial'] for method in requests), 'methods': requests}
    return types, inventory


def emit(aidl):
    types, inventory = load(aidl)
    outputs = {}
    method_ids = [SPDX, '#pragma once\n#include <cstdint>\nnamespace mindone::radio {\nenum class Method : uint32_t {\n']
    for domain in DOMAINS:
        title = domain.title()
        name = 'Radio' + title
        interface = 'IRadio' + title
        methods = inventory['services'][domain]['methods']
        headers = {f'aidl/android/hardware/radio/{domain}/Bn{name}.h', f'aidl/android/hardware/radio/{domain}/{interface}Response.h', f'aidl/android/hardware/radio/{domain}/{interface}Indication.h'}
        for method in methods:
            for arg in method['parameters']:
                base = arg['type'].removesuffix('[]')
                if '.' in base:
                    headers.add('aidl/' + Path(*type_path(aidl, base).relative_to(aidl).parts[3:]).with_suffix('').as_posix() + '.h')
        header = [SPDX, '#pragma once\n#include "Edge.h"\n#include "Methods.h"\n#include <optional>\n#include <tuple>\n#include <vector>\n#include <string>\n']
        header.extend(f'#include <{item}>\n' for item in sorted(headers))
        header.append(f'namespace mindone::radio {{\nclass {name} final : public ::aidl::android::hardware::radio::{domain}::Bn{name} {{\npublic:\n    explicit {name}(std::shared_ptr<Edge> edge) : edge_(std::move(edge)) {{}}\n')
        source = [SPDX, f'#include "{name}.h"\nnamespace mindone::radio {{\n']
        for method in methods:
            method_name = method['name']
            identifier = title + method_name[0].upper() + method_name[1:]
            method_ids.append(f'    {identifier},\n')
            parameters = method['parameters']
            arguments = ', '.join(f'{arg["cpp_parameter"]} p_{arg["name"]}' for arg in parameters)
            header.append(f'    ::ndk::ScopedAStatus {method_name}({arguments}) override;\n')
            if method['serial']:
                payload = [arg for arg in parameters if arg['name'] != 'serial']
                header.append(f'    using {method_name[0].upper() + method_name[1:]}Arguments = std::tuple<{", ".join(arg["cpp_type"] for arg in payload)}>;\n')
            source.append(f'::ndk::ScopedAStatus {name}::{method_name}({arguments}) {{\n')
            if method_name == 'setResponseFunctions':
                if len(parameters) != 2:
                    raise ValueError('Unexpected response registration signature')
                source.append(f'    edge_->replace(Service::{title}, p_{parameters[0]["name"]}, p_{parameters[1]["name"]});\n    return ::ndk::ScopedAStatus::ok();\n')
            elif method_name == 'responseAcknowledgement':
                source.append(f'    edge_->acknowledge(Service::{title});\n    return ::ndk::ScopedAStatus::ok();\n')
            else:
                args_type = method_name[0].upper() + method_name[1:] + 'Arguments'
                payload_args = ', '.join('p_' + arg['name'] for arg in payload)
                response = method['response']
                defaults = ', '.join(['info'] + ['{}' for arg in response['parameters'][1:]])
                source.append(f'    return edge_->request(Service::{title}, Method::{identifier}, p_serial,\n        std::make_any<{args_type}>({payload_args}),\n        [](const Snapshot& snapshot, const ResponseInfo& info, const mr_value*) {{\n            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::{domain}::{interface}Response>(snapshot.response);\n            return callback->{response["name"]}({defaults});\n        }});\n')
            source.append('}\n')
        header.append('private:\n    std::shared_ptr<Edge> edge_;\n};\n}\n')
        source.append('}\n')
        outputs[f'generated/{name}.h'] = ''.join(header)
        outputs[f'generated/{name}.cpp'] = ''.join(source)
    method_ids.append('};\n}\n')
    outputs['generated/Methods.h'] = ''.join(method_ids)
    outputs['generated/inventory.json'] = json.dumps(inventory, indent=2, sort_keys=True) + '\n'
    outputs.update(emit_routes(inventory, types, SPDX))
    return outputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--aidl-root', type=Path, default=DEFAULT_AIDL)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    header = next((parent / 'device/ikko/mindone/modem/radio/ffi/mindone_radio.h' for parent in ROOT.parents if (parent / 'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').exists()), None)
    if header is None:
        raise SystemExit('Cannot locate the C ABI header')
    accepted = set(re.findall(r'^#define (MR_REQUEST_\w+) ', header.read_text(), re.M))
    routed = [route['kind'] for route in ROUTES] + [alias for route in ROUTES for alias in route['aliases']]
    if len(routed) != len(set(routed)) or set(routed) & set(DEFERRED) or accepted != set(routed) | set(DEFERRED):
        raise SystemExit('Routed/deferred C ABI request coverage mismatch')
    outputs = emit(args.aidl_root)
    for relative, content in outputs.items():
        path = ROOT / relative
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise SystemExit(f'Generated file differs: {relative}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print(json.dumps({'status': 'pass', 'generated_files': len(outputs), 'check': args.check}))


if __name__ == '__main__':
    main()
