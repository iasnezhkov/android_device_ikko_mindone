/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include "generated/Methods.h"
#include "mindone_radio.h"
#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <any>
#include <functional>
#include <map>
#include <memory>
namespace ndk {
struct ScopedAStatus { static ScopedAStatus ok() { return {}; } };
struct ICInterface { virtual ~ICInterface() = default; };
}
namespace mindone::radio {
enum class Service { Config,Data,Messaging,Modem,Network,Sim,Voice,Ims,Count };
struct Snapshot { std::shared_ptr<::ndk::ICInterface> response; };
using ResponseInfo = ::aidl::android::hardware::radio::RadioResponseInfo;
using RadioError = ::aidl::android::hardware::radio::RadioError;
using Submit = std::function<int32_t(mr_runtime,uint64_t,const std::any&)>;
using Reply = std::function<::ndk::ScopedAStatus(const Snapshot&,const ResponseInfo&,const mr_value*)>;
struct Route {Submit submit; Reply reply;};
using Routes = std::map<std::pair<Service,Method>,Route>;
Routes makeRoutes(uint32_t uiccQueryKind=MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED);
}
