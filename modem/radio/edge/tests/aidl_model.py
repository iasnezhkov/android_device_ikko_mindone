# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

from pathlib import Path
import re
from generate import type_path

SPDX = '/*\n * SPDX-License-Identifier: Apache-2.0\n * SPDX-FileCopyrightText: 2026 The LineageOS Project\n */\n\n'
PRIMITIVES = {'boolean':'bool','byte':'int8_t','int':'int32_t','long':'int64_t','float':'float','double':'double','String':'std::string'}
FIXTURES = Path(__file__).resolve().parent / 'fixtures'


class Model:
    def __init__(self, aidl, output):
        self.aidl = aidl
        self.output = output
        self.done = set()

    def cpp(self, name, nullable=False):
        if name.endswith('[]'):
            result = 'std::vector<' + ('uint8_t' if name[:-2]=='byte' else self.cpp(name[:-2])) + '>'
        else:
            result = PRIMITIVES.get(name, '::aidl::' + name.replace('.', '::'))
        return 'std::optional<' + result + '>' if nullable else result

    def add(self, name):
        if name in self.done or name in PRIMITIVES:
            return
        self.done.add(name)
        parts = name.split('.')
        source = type_path(self.aidl, name)
        if source.stem != parts[-1]:
            self.add(name.rsplit('.', 1)[0])
            return
        text = re.sub(r'/\*.*?\*/|//[^\n]*','',source.read_text(),flags=re.S)
        text = text.replace('@nullable', 'Nullable')
        text = re.sub(r'@\w+(?:\([^)]*\))?','',text)
        match = re.search(r'\b(parcelable|union|enum)\s+(\w+)\s*\{(.*)\}',text,re.S)
        if not match:
            raise ValueError(name)
        category, simple, body = match.groups()
        nested=[]
        for enum in re.finditer(r'enum\s+(\w+)\s*\{[^}]*\}',body):
            nested.append(enum[1])
            self.done.add(name+'.'+enum[1])
        body = re.sub(r'enum\s+\w+\s*\{[^}]*\}','',body)
        fields=[]
        if category!='enum':
            for entry in body.split(';'):
                entry=entry.strip()
                if not entry or entry.startswith('const '):continue
                entry=entry.split('=')[0].strip()
                field=re.fullmatch(r'(Nullable\s+)?([\w.]+(?:\[\])?)\s+(\w+)',entry)
                if not field:raise ValueError((name,entry))
                fields.append((field[2],field[3],bool(field[1])))
        includes=[]
        for t,_,_ in fields:
            base=t.removesuffix('[]')
            if base not in PRIMITIVES and base not in self.done:
                self.add(base)
            if base not in PRIMITIVES and not base.startswith(name+'.'):
                includes.append('#include <aidl/'+base.replace('.','/')+'.h>\n')
        header=[SPDX,'#pragma once\n#include <cstdint>\n#include <string>\n#include <vector>\n#include <optional>\n#include <variant>\n#include <utility>\n',*includes,'namespace aidl::'+'::'.join(parts[:-1])+' {\n']
        if category=='enum':
            names = [entry.strip() for entry in body.split(',') if entry.strip()]
            header.append(f'enum class {simple} : int32_t {{ '+','.join(names)+' };\n')
        else:
            header.append(f'struct {simple} {{\n')
            for enum in nested:header.append(f'    enum class {enum} : int32_t {{ ModelValue = 0 }};\n')
            if name.endswith('.OperatorInfo'):header.append('    static constexpr int32_t STATUS_UNKNOWN = 0;\n')
            if category=='union':
                header.append('    enum class Tag { '+','.join(f for _,f,_ in fields)+' };\n')
                header.append('    std::variant<'+','.join(self.cpp(t,nullable) for t,_,nullable in fields)+'> value;\n')
                header.append('    template<Tag T, typename V> void set(V&& input) { value.template emplace<static_cast<size_t>(T)>(std::forward<V>(input)); }\n')
                header.append('    template<Tag T> const auto& get() const { return std::get<static_cast<size_t>(T)>(value); }\n')
                header.append('    Tag getTag() const { return static_cast<Tag>(value.index()); }\n')
            else:
                for t,f,nullable in fields:header.append('    '+self.cpp(t,nullable)+' '+f+'{};\n')
            header.append('};\n')
        header.append('}\n')
        path=self.output/'aidl'/Path(*parts).with_suffix('.h')
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(''.join(header))


def route_environment(model, generator, inventory, root):
    from route_schema import ROUTES
    output = model.output
    model.add('android.hardware.radio.RadioResponseInfo')
    model.add('android.hardware.radio.RadioError')
    edge = (FIXTURES / 'route-edge.h').read_text()
    (output/'Edge.h').write_text(edge)
    logging=output/'android-base/logging.h'
    logging.parent.mkdir(exist_ok=True)
    logging.write_text(SPDX+'#pragma once\n#include <cstdlib>\n#define CHECK(value) do { if (!(value)) std::abort(); } while (false)\n')
    types=generator.Types(model.aidl)
    fixtures=[SPDX,'#include "Edge.h"\n#include <cassert>\n#include <iostream>\n']
    for domain in generator.DOMAINS:
        selected=[route for route in ROUTES if route['service']==domain]
        title=domain.title()
        header=[SPDX,'#pragma once\n#include "Edge.h"\n#include <optional>\n#include <string>\n#include <tuple>\n#include <vector>\n']
        aliases=[]
        callbacks=[]
        for route in selected:
            method=next(item for item in inventory['services'][domain]['methods'] if item['name']==route['method'])
            for arg in method['parameters']+method['response']['parameters']:
                if arg['name']=='serial':continue
                base=arg['type'].removesuffix('[]')
                if '.' in base:
                    model.add(base)
                    header.append('#include <aidl/'+base.replace('.','/')+'.h>\n')
            name=route['method']; camel=name[0].upper()+name[1:]
            parameters=[arg for arg in method['parameters'] if arg['name']!='serial']
            aliases.append('using '+camel+'Arguments = std::tuple<'+','.join(types.cpp(arg) for arg in parameters)+'>;\n')
            response=method['response']
            signature=', '.join(types.parameter(arg)+(' info' if i==0 else '') for i,arg in enumerate(response['parameters']))
            callbacks.append('virtual ::ndk::ScopedAStatus '+response['name']+'('+signature+') { last=info; return ::ndk::ScopedAStatus::ok(); }\n')
        header.append('namespace aidl::android::hardware::radio::'+domain+' {\nstruct IRadio'+title+'Response : ::ndk::ICInterface { RadioResponseInfo last;\n'+''.join(callbacks)+'};\n}\n')
        header.append('namespace mindone::radio { struct Radio'+title+' {\n'+''.join(aliases)+'}; }\n')
        path=output/'generated'/('Radio'+title+'.h');path.parent.mkdir(exist_ok=True);path.write_text(''.join(header))
        fixtures.append('#include "generated/Radio'+title+'.h"\n')
    fixtures.append('using namespace mindone::radio;\nstatic mr_request submitted;\nstatic int submissions=0;\nextern "C" int32_t mr_submit(mr_runtime runtime,uint64_t id,const mr_request* request) { assert(runtime==7 && id==9); submitted=*request; ++submissions; return MR_OK; }\nint main() { auto routes=makeRoutes(); ResponseInfo info{}; info.error=RadioError::REQUEST_NOT_SUPPORTED; mr_value invalid{};\n')
    for route in ROUTES:
        domain=route['service'];title=domain.title();name=route['method'];camel=name[0].upper()+name[1:]
        fixtures.append('{ Radio'+title+'::'+camel+'Arguments args{};\n')
        if name=='setNumOfLiveModems':fixtures.append('std::get<0>(args)=1;\n')
        if name=='setLinkCapacityReportingCriteria':fixtures.append('std::get<5>(args)=static_cast<decltype(std::get<5>(args))::value_type>(1);\n'.replace('static_cast<decltype(std::get<5>(args))::value_type>(1)','static_cast<::aidl::android::hardware::radio::AccessNetwork>(1)'))
        if name=='setCarrierInfoForImsiEncryption':fixtures.append('std::get<0>(args).mcc="440"; std::get<0>(args).mnc="10"; std::get<0>(args).carrierKey={1}; std::get<0>(args).keyType=1;\n')
        fixtures.append(f'auto& route=routes.at({{Service::{title},Method::{title+camel}}});\nconst auto before=submissions; assert(route.submit(7,9,std::any(args))==MR_OK); assert(submissions==before+1 && submitted.kind=={route["kind"]});\n')
        expected = __import__('route_schema').SHAPES.get(route['shape'])
        if expected:
            fixtures.append(f'mr_value valid{{}}; valid.kind={expected[0]}; valid.card.gsm_umts_subscription_app_index=-1; valid.card.cdma_subscription_app_index=-1; valid.card.ims_subscription_app_index=-1;\n')
        fixtures.append('auto callback=std::make_shared<::aidl::android::hardware::radio::'+domain+'::IRadio'+title+'Response>(); Snapshot snapshot{callback}; info.error=RadioError::REQUEST_NOT_SUPPORTED; route.reply(snapshot,info,nullptr); assert(callback->last.error==RadioError::REQUEST_NOT_SUPPORTED); info.error=RadioError::NONE; route.reply(snapshot,info,&invalid); assert(callback->last.error==RadioError::INVALID_RESPONSE); \n')
        if expected:fixtures.append('route.reply(snapshot,info,&valid); assert(callback->last.error==RadioError::NONE);\n')
        fixtures.append('}\n')
    fixtures.append('for (const auto& [key, route] : routes) { static_cast<void>(route); assert(key.first != Service::Ims); }\n')
    fixtures.append('auto& invalidRoute=routes.at({Service::Network,Method::NetworkSetLinkCapacityReportingCriteria}); const auto before=submissions; assert(invalidRoute.submit(7,9,std::any(RadioNetwork::SetLinkCapacityReportingCriteriaArguments{}))==MR_INVALID_ARGUMENT && submissions==before);\nstd::cout << "39 route encoders and error/tag checks passed\\n"; }\n')
    fixtures[-1] = fixtures[-1].replace('std::cout', 'auto rejected=[&](Service service,Method method,std::any args) { const auto before=submissions; assert(routes.at({service,method}).submit(7,9,args)==MR_INVALID_ARGUMENT && submissions==before); };\nRadioModem::SendDeviceStateArguments device{}; std::get<0>(device)=static_cast<::aidl::android::hardware::radio::modem::DeviceStateType>(3); rejected(Service::Modem,Method::ModemSendDeviceState,device);\nRadioVoice::SetTtyModeArguments tty{static_cast<::aidl::android::hardware::radio::voice::TtyMode>(4)}; rejected(Service::Voice,Method::VoiceSetTtyMode,tty);\nrejected(Service::Config,Method::ConfigSetNumOfLiveModems,RadioConfig::SetNumOfLiveModemsArguments{});\nRadioSim::SetCarrierInfoForImsiEncryptionArguments key{}; rejected(Service::Sim,Method::SimSetCarrierInfoForImsiEncryption,key);\nRadioModem::SetRadioPowerArguments power{false,true,true}; assert(routes.at({Service::Modem,Method::ModemSetRadioPower}).submit(7,9,std::any(power))==MR_OK && !submitted.for_emergency_call && !submitted.preferred_for_emergency_call);\nstd::cout')
    fixtures[-1] = fixtures[-1].replace('std::cout', 'auto alternate=makeRoutes(MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT); assert(alternate.at({Service::Sim,Method::SimAreUiccApplicationsEnabled}).submit(7,9,std::any(RadioSim::AreUiccApplicationsEnabledArguments{}))==MR_OK && submitted.kind==MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT);\nstd::cout')
    (output/'route-tests.cpp').write_text(''.join(fixtures))
    (output/'Routes.cpp').write_text((root/'Routes.cpp').read_text())


def ims_environment(model, generator, inventory, root):
    output = model.output
    methods = inventory['services']['ims']['methods']
    types = generator.Types(model.aidl)
    model.add('android.hardware.radio.RadioResponseInfo')
    model.add('android.hardware.radio.RadioError')
    includes = {'#include "Edge.h"\n'}
    for method in methods:
        for arg in method['parameters'] + method.get('response', {}).get('parameters', []):
            base = arg['type'].removesuffix('[]')
            if '.' in base and types.category(base) != 'interface':
                model.add(base)
                path = generator.type_path(model.aidl, base)
                includes.add('#include <aidl/' + Path(*path.relative_to(model.aidl).parts[3:]).with_suffix('.h').as_posix() + '>\n')
    prefix = SPDX + '#pragma once\n' + ''.join(sorted(includes))
    namespace = 'namespace aidl::android::hardware::radio::ims {\n'
    response = [prefix, namespace, 'struct IRadioImsResponse : ::ndk::ICInterface { int calls = 0; int serial = 0; std::string method;\n']
    base = [prefix, '#include "IRadioImsResponse.h"\n#include "IRadioImsIndication.h"\n', namespace, 'struct BnRadioIms : ::ndk::ICInterface {\n']
    fixture = [SPDX, '#include "RadioIms.h"\n#include <cassert>\nusing namespace mindone::radio;\nint main() { auto edge = std::make_shared<Edge>(); RadioIms radio(edge); auto response = std::make_shared<::aidl::android::hardware::radio::ims::IRadioImsResponse>(); auto indication = std::make_shared<::aidl::android::hardware::radio::ims::IRadioImsIndication>(); radio.setResponseFunctions(response, indication); assert(edge->snapshot.response == response && edge->indication == indication);\n']
    for method in methods:
        signature = ', '.join(types.parameter(arg) for arg in method['parameters'])
        base.append('virtual ::ndk::ScopedAStatus ' + method['name'] + '(' + signature + ') = 0;\n')
        if not method['serial']:
            continue
        reply = method['response']
        signature = ', '.join(types.parameter(arg) + ' p_' + arg['name'] for arg in reply['parameters'])
        body = 'assert(p_info.error == ::aidl::android::hardware::radio::RadioError::REQUEST_NOT_SUPPORTED); ++calls; serial = p_info.serial; method = "' + reply['name'] + '"; '
        if method['name'] == 'startImsTraffic':
            body += 'assert(!p_failureInfo.has_value()); '
        response.append('::ndk::ScopedAStatus ' + reply['name'] + '(' + signature + ') { ' + body + 'return ::ndk::ScopedAStatus::ok(); }\n')
        args = ', '.join('42' if arg['name'] == 'serial' else '{}' for arg in method['parameters'])
        fixture.append('{ const auto before = response->calls; radio.' + method['name'] + '(' + args + '); assert(response->calls == before + 1 && response->serial == 42 && response->method == "' + reply['name'] + '"); }\n')
    response.append('};\n}\n')
    base.append('};\n}\n')
    fixture.append('assert(edge->calls == 7 && response->calls == 7); }\n')
    directory = output / 'aidl/android/hardware/radio/ims'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'IRadioImsResponse.h').write_text(''.join(response))
    (directory / 'IRadioImsIndication.h').write_text(prefix + namespace + 'struct IRadioImsIndication : ::ndk::ICInterface {};\n}\n')
    (directory / 'BnRadioIms.h').write_text(''.join(base))
    (output / 'Edge.h').write_text((FIXTURES / 'ims-edge.h').read_text())
    (output / 'route-edge.h').write_text((FIXTURES / 'route-edge.h').read_text())
    (output / 'generated').mkdir(exist_ok=True)
    for name in ('Methods.h', 'RadioIms.h', 'RadioIms.cpp'):
        (output / 'generated' / name).write_text((root / 'generated' / name).read_text())
    (output / 'ims-tests.cpp').write_text(''.join(fixture))


def indication_environment(model, root):
    output = model.output
    model.add('android.hardware.radio.RadioIndicationType')
    model.add('android.hardware.radio.modem.RadioState')
    common = (FIXTURES / 'indication-callbacks.h').read_text()
    (output/'Callbacks.h').write_text(common)
    for domain in ('Modem','Network','Sim'):
        methods = {
            'Modem': '::ndk::ScopedAStatus rilConnected(T) { events.push_back("connected"); return {}; }\n::ndk::ScopedAStatus radioStateChanged(T, RadioState state) { events.push_back("state:"+std::to_string(static_cast<int32_t>(state))); return {}; }\n::ndk::ScopedAStatus modemReset(T, const std::string&) { events.push_back("reset"); return {}; }\n',
            'Network': '::ndk::ScopedAStatus networkStateChanged(T) { events.push_back("network"); return {}; }\n::ndk::ScopedAStatus currentSignalStrength(T, const SignalStrength& signal) { events.push_back("signal:"+std::to_string(signal.lte.rsrp)); return {}; }\n',
            'Sim': '::ndk::ScopedAStatus simStatusChanged(T) { events.push_back("sim"); return {}; }\n',
        }
        content = SPDX + '#pragma once\n#include "Callbacks.h"\n#include <aidl/android/hardware/radio/RadioIndicationType.h>\n#include <aidl/android/hardware/radio/modem/RadioState.h>\n#include <aidl/android/hardware/radio/network/SignalStrength.h>\nnamespace aidl::android::hardware::radio::'+domain.lower()+' {\nstruct IRadio'+domain+'Indication : ::ndk::ICInterface {\nusing T = ::aidl::android::hardware::radio::RadioIndicationType;\n'+methods[domain].replace('events.', '::mindone::radio::events.')+'};\n}\n'
        path = output/'aidl/android/hardware/radio'/domain.lower()/('IRadio'+domain+'Indication.h')
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(content)
    logging=output/'android-base/logging.h'
    logging.parent.mkdir(exist_ok=True)
    logging.write_text(SPDX+'#pragma once\n#include <iostream>\n#define LOG(level) std::cerr\n')
    for name in ('Indications.cpp','Indications.h'):
        (output/name).write_text((root/name).read_text())
