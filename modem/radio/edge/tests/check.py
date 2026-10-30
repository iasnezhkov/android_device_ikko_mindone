# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import importlib.util
import json
import tempfile
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('edge_generate', ROOT / 'generate.py')
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)
AIDL = GENERATOR.DEFAULT_AIDL


def declarations(path):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    result = re.findall(r'\bvoid\s+(\w+)\s*\((.*?)\)\s*;', text, flags=re.S)
    names = [name for name, _ in result]
    if len(names) != len(set(names)):
        raise AssertionError(f'Duplicate source methods: {path}')
    return result


class GenerationTests(unittest.TestCase):
    def test_checked_in_generation(self):
        subprocess.run([sys.executable, '-B', str(ROOT / 'generate.py'), '--check', '--aidl-root', str(AIDL)], check=True, capture_output=True, text=True)

    def test_determinism(self):
        self.assertEqual(GENERATOR.emit(AIDL), GENERATOR.emit(AIDL))

    def test_generated_file_inventory(self):
        expected = set(GENERATOR.emit(AIDL))
        actual = {str(path.relative_to(ROOT)) for path in (ROOT / 'generated').iterdir() if path.is_file()}
        self.assertEqual(expected, actual)

    def test_methods_and_responses(self):
        inventory = json.loads((ROOT / 'generated/inventory.json').read_text())
        for domain in GENERATOR.DOMAINS:
            with self.subTest(domain=domain):
                name = 'Radio' + domain.title()
                requests = declarations(GENERATOR.interface_path(AIDL, domain))
                responses = dict(declarations(GENERATOR.interface_path(AIDL, domain, 'Response')))
                header = (ROOT / 'generated' / (name + '.h')).read_text()
                source = (ROOT / 'generated' / (name + '.cpp')).read_text()
                declared = re.findall(r'ScopedAStatus\s+(\w+)\([^;]*?\)\s+override;', header)
                defined = re.findall(r'ScopedAStatus\s+' + name + r'::(\w+)\(', source)
                expected = [method for method, _ in requests]
                self.assertEqual(declared, expected)
                self.assertEqual(defined, expected)
                self.assertEqual(len(set(declared)), len(declared))
                recorded = inventory['services'][domain]
                self.assertEqual(recorded['aidl_version'], GENERATOR.DOMAIN_VERSIONS[domain])
                self.assertEqual(recorded['method_count'], len(expected))
                self.assertEqual([method['name'] for method in recorded['methods']], expected)
                serial_count = 0
                for method, parameters in requests:
                    is_serial = bool(re.search(r'\bint\s+serial\b', parameters))
                    if is_serial:
                        serial_count += 1
                        response = 'hangupConnectionResponse' if (domain, method) == ('voice', 'hangup') else method + 'Response'
                        self.assertIn(response, responses)
                        self.assertRegex(responses[response], r'^\s*(?:in\s+)?android\.hardware\.radio\.RadioResponseInfo\s+\w+')
                        self.assertEqual(source.count('callback->' + response + '('), 1)
                        self.assertIn('using ' + method[0].upper() + method[1:] + 'Arguments = std::tuple<', header)
                    else:
                        self.assertIn(method, ('setResponseFunctions', 'responseAcknowledgement'))
                self.assertEqual(recorded['serial_method_count'], serial_count)
                self.assertEqual(source.count('edge_->request('), serial_count)

    def test_route_coverage(self):
        repository = next(parent for parent in ROOT.parents if (parent/'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').exists())
        header = (repository/'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').read_text()
        kinds = set(re.findall(r'^#define (MR_REQUEST_\w+) ',header,re.M))
        values = set(re.findall(r'^#define (MR_VALUE_\w+) ',header,re.M))
        coverage = json.loads((ROOT/'generated/routes.json').read_text())['routes']
        routed = [entry['kind'] for entry in coverage] + [alias for entry in coverage for alias in entry['aliases']]
        self.assertEqual(kinds,set(routed) | set(json.loads((ROOT/'generated/routes.json').read_text())['deferred_request_kinds']))
        self.assertEqual(len(routed),40)
        self.assertEqual(len(coverage),39)
        self.assertEqual(len(routed),len(set(routed)))
        self.assertEqual(values,{entry['value_kind'] for entry in coverage if entry['value_kind']})
        pairs = [(entry['service'],entry['method']) for entry in coverage]
        self.assertEqual(len(pairs),len(set(pairs)))
        inventory = json.loads((ROOT/'generated/inventory.json').read_text())
        for entry in coverage:
            self.assertIn(entry['method'],[method['name'] for method in inventory['services'][entry['service']]['methods']])
        self.assertFalse(any(entry['service'] == 'ims' for entry in coverage))
        self.assertEqual(inventory['services']['ims']['serial_method_count'], 7)
        source = (ROOT/'generated/Routes.inc').read_text()
        self.assertEqual(source.count('return mr_submit('),len(coverage))
        self.assertNotIn('switch (value->kind)',source)
        self.assertIn('RadioError::INVALID_RESPONSE',source)
        self.assertIn('uiccQueryKind',source)

    def test_host_conversions(self):
        model_spec = importlib.util.spec_from_file_location('aidl_model',ROOT/'tests/aidl_model.py')
        model_module = importlib.util.module_from_spec(model_spec)
        model_spec.loader.exec_module(model_module)
        repository = next(parent for parent in ROOT.parents if (parent/'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').exists())
        with tempfile.TemporaryDirectory(prefix='.host-check-',dir=ROOT/'tests') as directory:
            output=Path(directory)
            model=model_module.Model(AIDL,output)
            header=(ROOT/'Conversions.h').read_text()
            for name in re.findall(r'#include <aidl/(.*)\.h>',header):
                model.add(name.replace('/','.'))
            binary=output/'conversions'
            command=[os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(output),'-I',str(ROOT),'-I',str(repository/'device/ikko/mindone/modem/radio/ffi'),str(ROOT/'Conversions.cpp'),str(ROOT/'tests/conversions.cpp'),'-o',str(binary)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(binary)],check=True,capture_output=True,text=True)

    def test_host_routes(self):
        model_spec = importlib.util.spec_from_file_location('aidl_model',ROOT/'tests/aidl_model.py')
        model_module = importlib.util.module_from_spec(model_spec)
        model_spec.loader.exec_module(model_module)
        repository = next(parent for parent in ROOT.parents if (parent/'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').exists())
        with tempfile.TemporaryDirectory(prefix='.host-check-',dir=ROOT/'tests') as directory:
            output=Path(directory)
            model=model_module.Model(AIDL,output)
            for name in re.findall(r'#include <aidl/(.*)\.h>',(ROOT/'Conversions.h').read_text()):
                model.add(name.replace('/','.'))
            inventory=json.loads((ROOT/'generated/inventory.json').read_text())
            model_module.route_environment(model,GENERATOR,inventory,ROOT)
            binary=output/'routes'
            command=[os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(output),'-I',str(ROOT),'-I',str(repository/'device/ikko/mindone/modem/radio/ffi'),str(ROOT/'Conversions.cpp'),str(output/'Routes.cpp'),str(output/'route-tests.cpp'),'-o',str(binary)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(binary)],check=True,capture_output=True,text=True)

    def test_host_indications(self):
        model_spec = importlib.util.spec_from_file_location('aidl_model',ROOT/'tests/aidl_model.py')
        model_module = importlib.util.module_from_spec(model_spec)
        model_spec.loader.exec_module(model_module)
        repository = next(parent for parent in ROOT.parents if (parent/'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').exists())
        with tempfile.TemporaryDirectory(prefix='.host-check-',dir=ROOT/'tests') as directory:
            output=Path(directory)
            model=model_module.Model(AIDL,output)
            for name in re.findall(r'#include <aidl/(.*)\.h>',(ROOT/'Conversions.h').read_text()):
                model.add(name.replace('/','.'))
            model_module.indication_environment(model,ROOT)
            binary=output/'indications'
            command=[os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(output),'-I',str(ROOT),'-I',str(repository/'device/ikko/mindone/modem/radio/ffi'),str(ROOT/'Conversions.cpp'),str(output/'Indications.cpp'),str(ROOT/'tests/indications.cpp'),'-o',str(binary)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(binary)],check=True,capture_output=True,text=True)

    def test_host_ims_fallbacks(self):
        model_spec = importlib.util.spec_from_file_location('aidl_model', ROOT / 'tests/aidl_model.py')
        model_module = importlib.util.module_from_spec(model_spec)
        model_spec.loader.exec_module(model_module)
        repository = next(parent for parent in ROOT.parents if (parent / 'device/ikko/mindone/modem/radio/ffi/mindone_radio.h').exists())
        with tempfile.TemporaryDirectory(prefix='.host-check-', dir=ROOT / 'tests') as directory:
            output = Path(directory)
            model = model_module.Model(AIDL, output)
            inventory = json.loads((ROOT / 'generated/inventory.json').read_text())
            model_module.ims_environment(model, GENERATOR, inventory, ROOT)
            binary = output / 'ims'
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(output), '-I', str(output / 'generated'), '-I', str(repository / 'device/ikko/mindone/modem/radio/ffi'), str(output / 'generated/RadioIms.cpp'), str(output / 'ims-tests.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_ndk_type_mapping(self):
        types = GENERATOR.Types(AIDL)
        self.assertEqual(types.parameter({'type':'android.hardware.radio.AccessNetwork','nullable':False}), '::aidl::android::hardware::radio::AccessNetwork')
        self.assertEqual(types.cpp({'type':'byte[]','nullable':False}), 'std::vector<uint8_t>')
        self.assertEqual(types.parameter({'type':'android.hardware.radio.data.DataProfileInfo','nullable':True}), 'const std::optional<::aidl::android::hardware::radio::data::DataProfileInfo>&')
        self.assertEqual(types.parameter({'type':'android.hardware.radio.sim.IRadioSimResponse','nullable':False}), 'const std::shared_ptr<::aidl::android::hardware::radio::sim::IRadioSimResponse>&')

    def test_ims_ndk_types(self):
        types = GENERATOR.Types(AIDL)
        for name, expected in (
            ('ImsCall.Direction', '::aidl::android::hardware::radio::ims::ImsCall::Direction'),
            ('IRadioImsResponse', 'const std::shared_ptr<::aidl::android::hardware::radio::ims::IRadioImsResponse>&'),
            ('ConnectionFailureInfo', 'const ::aidl::android::hardware::radio::ims::ConnectionFailureInfo&'),
        ):
            self.assertEqual(types.parameter({'type': 'android.hardware.radio.ims.' + name, 'nullable': False}), expected)
        self.assertEqual(types.cpp({'type': 'android.hardware.radio.ims.ConnectionFailureInfo', 'nullable': True}),
                         'std::optional<::aidl::android::hardware::radio::ims::ConnectionFailureInfo>')
        source = (ROOT / 'generated/RadioIms.cpp').read_text()
        self.assertIn('edge_->replace(Service::Ims, p_radioImsResponse, p_radioImsIndication)', source)
        self.assertEqual(source.count('edge_->request(Service::Ims,'), 7)
        header = (ROOT / 'generated/RadioIms.h').read_text()
        self.assertIn('#include <aidl/android/hardware/radio/ims/ImsCall.h>', header)
        self.assertNotIn('ImsCall/Direction.h', header)

    def test_registration_files(self):
        xml = ET.parse(ROOT / 'mindone-radio.xml').getroot()
        rc = (ROOT / 'mindone-radio.rc').read_text()
        main = (ROOT / 'main.cpp').read_text()
        bp = (ROOT / 'Android.bp').read_text()
        entries = []
        for hal in xml.findall('hal'):
            self.assertEqual(hal.attrib['format'], 'aidl')
            domain = hal.findtext('name').rsplit('.', 1)[-1]
            self.assertEqual(hal.findtext('version'), str(GENERATOR.DOMAIN_VERSIONS[domain]))
            entry = hal.findtext('name') + '.' + hal.findtext('fqname')
            entries.append(entry)
            self.assertEqual(rc.count('interface aidl ' + entry + '\n'), 1)
        expected = [f'android.hardware.radio.{domain}.IRadio{domain.title()}/{"default" if domain == "config" else "slot1"}' for domain in GENERATOR.DOMAINS]
        self.assertEqual(entries, expected)
        self.assertEqual(len(set(entries)), 8)
        self.assertIn('ABinderProcess_setThreadPoolMaxThreadCount(4)', main)
        for domain in GENERATOR.DOMAINS:
            self.assertIn(f'android.hardware.radio.{domain}-V{GENERATOR.DOMAIN_VERSIONS[domain]}-ndk', bp)
            self.assertIn('publish<mindone::radio::Radio' + domain.title() + '>', main)

    def test_untimed_workers(self):
        source = (ROOT / 'Edge.cpp').read_text()
        header = (ROOT / 'Edge.h').read_text()
        waits = re.findall(r'mr_next_batch\(runtime_,\s*([^,]+),', source)
        self.assertEqual(waits, ['MR_WAIT_FOREVER'])
        self.assertIn('std::condition_variable localChanged_', header)
        self.assertIn('localChanged_.wait(lock,', source)
        self.assertNotRegex(source, r'wait_for|wait_until|sleep_for|sleep_until|MR_TIMEOUT')
        self.assertIn('void Edge::localDispatch()', source)
        self.assertIn('localWorker_.join()', source)
        stop = source[source.index('void Edge::stop()'):source.index('void Edge::replace(')]
        self.assertLess(stop.index('mr_stop(runtime)'), stop.index('dispatcher_.join()'))
        self.assertLess(stop.index('dispatcher_.join()'), stop.index('localStopping_ = true'))
        self.assertLess(stop.index('localWorker_.join()'), stop.index('mr_destroy(&runtime)'))
        self.assertIn('RadioError::RADIO_NOT_AVAILABLE', stop)
        dispatch = source[source.index('void Edge::dispatch()'):]
        self.assertIn('status != MR_STOPPED', dispatch)
        self.assertNotIn('initials.swap', dispatch)

    def test_dispatch_contract(self):
        source = (ROOT / 'Edge.cpp').read_text()
        self.assertIn('RadioError::REQUEST_NOT_SUPPORTED', source)
        self.assertIn('completions_.push_back', source)
        self.assertIn('RadioResponseType::SOLICITED', source)
        self.assertIn('mr_next_batch(', source)
        self.assertIn('mr_batch_free(', source)
        self.assertIn('invocation.snapshot.current()', source)
        self.assertNotIn('mr_submit(', source)
        self.assertIn('#include "generated/Routes.inc"', (ROOT / 'Routes.cpp').read_text())
        callbacks = (ROOT / 'Callbacks.cpp').read_text()
        self.assertIn('AIBinder_DeathRecipient_setOnUnlinked', callbacks)
        self.assertIn('AIBinder_linkToDeath', callbacks)
        indication_source = (ROOT / 'Indications.cpp').read_text()
        self.assertIn('RadioIndicationType::UNSOLICITED', indication_source)
        self.assertNotIn('ACK_EXP', source + indication_source)


def main():
    global AIDL
    parser = argparse.ArgumentParser()
    parser.add_argument('--aidl-root', type=Path, default=AIDL)
    args = parser.parse_args()
    AIDL = args.aidl_root
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GenerationTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    inventory = json.loads((ROOT / 'generated/inventory.json').read_text())
    print(json.dumps({'status':'pass' if result.wasSuccessful() else 'fail', 'tests':result.testsRun, 'methods':{domain:service['method_count'] for domain, service in inventory['services'].items()}}))
    raise SystemExit(0 if result.wasSuccessful() else 1)


if __name__ == '__main__':
    main()
