# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import json
import re
from pathlib import Path
from route_schema import ROUTES, SHAPES, DEFERRED


def emit_routes(inventory, types, spdx):
    source = [spdx, 'Routes makeRoutes(uint32_t uiccQueryKind) {\n    CHECK(uiccQueryKind == MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED || uiccQueryKind == MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT);\n    Routes routes;\n']
    coverage = []
    pairs = [(route['service'], route['method']) for route in ROUTES]
    if len(pairs) != len(set(pairs)):
        raise ValueError('Duplicate routed service/method')
    for route in ROUTES:
        domain, name = route['service'], route['method']
        title = domain.title()
        found = [m for m in inventory['services'][domain]['methods'] if m['name']==name and m['serial']]
        if len(found)!=1:
            raise ValueError(f'Unknown routed method: {domain}.{name}')
        method = found[0]
        response = method['response']
        identifier = title+name[0].upper()+name[1:]
        tuple_type = f'Radio{title}::{name[0].upper()+name[1:]}Arguments'
        source.append(f'    routes.emplace(std::make_pair(Service::{title}, Method::{identifier}), Route{{\n')
        capture = '[uiccQueryKind]' if route['aliases'] else '[]'
        source.append(f'        {capture}(mr_runtime runtime, uint64_t id, const std::any& payload) {{\n            const auto* args = std::any_cast<{tuple_type}>(&payload);\n            if (!args) return MR_INVALID_ARGUMENT;\n            RequestStorage storage;\n')
        kind = 'uiccQueryKind' if route['aliases'] else route['kind']
        source.append(f'            storage.request.kind = {kind};\n')
        if route['encoder']:
            source.append(f'            if (!{route["encoder"]}(*args, storage)) return MR_INVALID_ARGUMENT;\n')
        elif route['field']:
            if route['validation']:
                source.append(f'            if (!validation::{route["validation"]}(static_cast<int32_t>(std::get<0>(*args)))) return MR_INVALID_ARGUMENT;\n')
            field = route['field']
            cast = {'mask':'uint32_t','integer':'int32_t','small_integer':'int8_t','boolean':'uint8_t'}[field]
            source.append(f'            storage.request.{field} = static_cast<{cast}>(std::get<0>(*args));\n')
        source.append('            return mr_submit(runtime, id, &storage.request);\n        },\n')
        source.append(f'        [](const Snapshot& snapshot, const ResponseInfo& original, const mr_value* value) {{\n            auto callback = std::static_pointer_cast<::aidl::android::hardware::radio::{domain}::IRadio{title}Response>(snapshot.response);\n            ResponseInfo info = original;\n')
        outputs = response['parameters'][1:]
        for i,arg in enumerate(outputs):
            source.append(f'            {types.cpp(arg)} out{i}{{}};\n')
        shape = SHAPES.get(route['shape'])
        source.append('            if (info.error == RadioError::NONE) {\n')
        if shape:
            expected, fields = shape
            if len(fields)!=len(outputs):raise ValueError(f'Response shape mismatch: {domain}.{name}')
            exprs=[f'convert::{function}({argument}, out{i})' for i,(function,argument) in enumerate(fields)]
            valid=' && '.join(exprs) or 'true'
            source.append(f'                if (!value || value->kind != {expected} || !({valid})) info.error = RadioError::INVALID_RESPONSE;\n')
        else:
            source.append('                static_cast<void>(value);\n                info.error = RadioError::INVALID_RESPONSE;\n')
        source.append('            }\n')
        args=', '.join(['info']+[f'out{i}' for i in range(len(outputs))])
        source.append(f'            return callback->{response["name"]}({args});\n        }}\n    }});\n')
        coverage.append({**route,'response_method':response['name'],'value_kind':shape[0] if shape else None})
    source.append('    return routes;\n}\n')
    return {'generated/Routes.inc':''.join(source),'generated/routes.json':json.dumps({'routes':coverage,'deferred_request_kinds':DEFERRED},indent=2,sort_keys=True)+'\n'}
