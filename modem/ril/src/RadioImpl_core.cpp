/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mind_one minimal RIL, phase 1 skeleton (RIL-MINIMAL-1409).
//
// Hand-written, faithful implementations of the task's minimal request set (SIM status, signal
// strength, voice/data registration, operator, dial/hangup/currentCalls/acceptCall, sendSms,
// setupDataCall/deactivateDataCall in all their versioned forms, setRadioPower, getDeviceIdentity)
// plus the two IRadio plumbing methods (setResponseFunctions, responseAcknowledgement) and the
// boot handshake. See RadioImpl_stubs.cpp for the other ~174 (auto-generated
// REQUEST_NOT_SUPPORTED responses) and RadioImpl.h for the class layout and version rationale.
//
// Scope, stated plainly rather than left implicit (this is a phase-1 skeleton, not a finished
// RIL):
//  - Single SIM application is assumed (no multi-app CardStatus enumeration).
//  - getOperator answers the same AT+COPS? string for long/short/numeric -- a real
//    implementation queries all three formats (AT+COPS=3,0 / =3,1 / =3,2 then AT+COPS? each
//    time), not done here.
//  - getVoiceRegistrationState/getDataRegistrationState use AT+CREG?/AT+CGREG? only; MTK's
//    richer AT+ECREG/AT+ECGREG/AT+ECEREG variants (RIL-MINIMAL-1409 AT map) and LTE-only
//    AT+CEREG are not queried -- phase 2.
//  - getCurrentCalls/hangup assume a single active call slot addressed by its GSM index; CDMA,
//    multiparty conferencing beyond +CHLD's own semantics, and video calls are out of scope.
//  - setupDataCall implements exactly one default-bearer IPv4 PDN on a fixed context id; no
//    IPv6/IPv4v6, no non-default DataRequestReason, no APN authentication. The interface name
//    returned ("ccmni0") is a best-effort placeholder, NOT confirmed against a live data call --
//    MODEM-STACK-1409 S2.7 explicitly could not trace the PDN-to-ccmniN assignment
//    mechanism from kernel source alone; resolving it needs a live AT trace (a phase-2 item,
//    also called out in RIL-MINIMAL-1409).
//  - CellIdentity, AccessTechnologySpecificInfo (1.6 safe unions) and other structurally-required
//    but not-yet-parsed fields are left at their HIDL-default ("noinit"/empty) value -- spec-legal,
//    just not informative yet.
#include "RadioImpl.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>

#undef LOG_TAG
#define LOG_TAG "mindone_ril"
#include <cutils/properties.h>
#include <log/log.h>

extern "C" {
#include "at_tok.h"
}

namespace mindone::ril {

namespace V1_0 = ::android::hardware::radio::V1_0;
namespace V1_2 = ::android::hardware::radio::V1_2;
namespace V1_4 = ::android::hardware::radio::V1_4;
namespace V1_5 = ::android::hardware::radio::V1_5;
namespace V1_6 = ::android::hardware::radio::V1_6;
using ::android::hardware::Void;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;

// --------------------------------------------------------------------------------------------
// small local AT-line parsing helpers (built on the ported at_tok.{c,h}, see AtChannel.h)
// --------------------------------------------------------------------------------------------
namespace {

// Runs at_tok_start + at_tok_nextint over a mutable copy of `line`, returns -1 on any parse
// failure without touching *out.
bool tokNextInt(char** cur, int* out) { return at_tok_nextint(cur, out) == 0; }

std::string cmeErrorNote(const AtResult& r) {
    return r.cmeError >= 0 ? (" (+CME ERROR " + std::to_string(r.cmeError) + ")") : "";
}

}  // namespace

// --------------------------------------------------------------------------------------------
// helpers declared in RadioImpl.h
// --------------------------------------------------------------------------------------------

V1_0::RadioResponseInfo MindoneRadio::mkInfo(int32_t serial, V1_0::RadioError error) {
    return V1_0::RadioResponseInfo{V1_0::RadioResponseType::SOLICITED, serial, error};
}

V1_6::RadioResponseInfo MindoneRadio::mkInfo16(int32_t serial, V1_6::RadioError error) {
    return V1_6::RadioResponseInfo{V1_0::RadioResponseType::SOLICITED, serial, error};
}

void MindoneRadio::logStubRequest(const char* name, int32_t serial) {
    ALOGI("%s(serial=%d): not implemented in phase 1, answering REQUEST_NOT_SUPPORTED", name,
          serial);
}

MindoneRadio::MindoneRadio() = default;
MindoneRadio::~MindoneRadio() = default;

bool MindoneRadio::openAtChannels(const std::string& cmdDevicePath,
                                   const std::string& notiDevicePath) {
    int cmdFd = ::open(cmdDevicePath.c_str(), O_RDWR | O_NOCTTY);
    if (cmdFd < 0) {
        ALOGE("openAtChannels: open(%s) failed: %s", cmdDevicePath.c_str(), strerror(errno));
        return false;
    }
    int notiFd = ::open(notiDevicePath.c_str(), O_RDONLY | O_NOCTTY);
    if (notiFd < 0) {
        ALOGE("openAtChannels: open(%s) failed: %s", notiDevicePath.c_str(), strerror(errno));
        ::close(cmdFd);
        return false;
    }

    auto onCmdChannelUrc = [](const std::string& line, const std::string& pdu) {
        // Should be rare (see AtChannel.h "one deliberate adaptation") -- the mux is supposed to
        // keep URCs off the command channels, but log rather than drop if the stock mux ever
        // interleaves one here (e.g. during channel setup before the mux is fully steady).
        ALOGW("URC on command channel (unexpected): %s", line.c_str());
        (void)pdu;
    };
    auto onNotiUrc = [](const std::string& line, const std::string& pdu) {
        // Phase-1 URC handling is intentionally minimal -- see RIL-MINIMAL-1409
        // "phase 2 plan". Everything is logged so a live capture can grow this list.
        ALOGI("URC: %s%s", line.c_str(), pdu.empty() ? "" : (" / PDU=" + pdu).c_str());
    };
    auto onClosed = [] { ALOGE("AT channel closed unexpectedly"); };

    if (!mCmdChannel.open(cmdFd, /*urcMode=*/false, onCmdChannelUrc, onClosed)) return false;
    if (!mNotiChannel.open(notiFd, /*urcMode=*/true, onNotiUrc, onClosed)) return false;
    return true;
}

::android::hardware::Return<void> MindoneRadio::setResponseFunctions(
    const ::android::sp<V1_0::IRadioResponse>& radioResponse,
    const ::android::sp<V1_0::IRadioIndication>& radioIndication) {
    ALOGI("setResponseFunctions: framework (re)connected");
    mResponseV1_0 = radioResponse;
    mIndicationV1_0 = radioIndication;
    mResponseV1_6 = V1_6::IRadioResponse::castFrom(radioResponse).withDefault(nullptr);
    mIndicationV1_6 = V1_6::IRadioIndication::castFrom(radioIndication).withDefault(nullptr);
    ALOGI("setResponseFunctions: caller %s a 1.6-capable IRadioResponse/IRadioIndication pair",
          mResponseV1_6 != nullptr ? "provided" : "did NOT provide");

    if (!mBootHandshakeDone) {
        runBootHandshake();
        mBootHandshakeDone = true;
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::responseAcknowledgement() {
    // The framework only calls this after we set RadioResponseInfo.type ==
    // SOLICITED_ACK_EXP, which this skeleton never does (see RadioImpl.h) -- present only
    // because IRadio requires every pure virtual overridden. Real bodies belong in phase 2 if a
    // request ever needs flow-controlled acking (long-running SIM I/O, per the HIDL doc comment
    // on this method).
    ALOGV("responseAcknowledgement (unexpected on this skeleton -- SOLICITED_ACK_EXP unused)");
    return Void();
}

void MindoneRadio::runBootHandshake() {
    // Recovered from the STOCK RIL's real boot sequence, not the task's originally-guessed
    // command names (three of which -- AT+EIND, AT+EMDSTATUS -- do not exist in the binary at
    // all; see modem/ril/AT-MAP-NOTES.md "Boot-handshake findings" and
    // RIL-MINIMAL-1409). The real handshake lives in `RmcRadioRequestHandler`'s
    // constructor (address 0x424be4 in libmtk-ril.so), fired once per SIM slot at RIL bring-up.
    // Every line here is a runtime-mode set or a plain read -- NONE of them is a persistent
    // NVRAM/IMEI write (see RIL-MINIMAL-1409 "risks" for the AT+E* write-shaped commands
    // this project blacklists outright, e.g. any AT+EGMR with the write-mode first argument).
    struct Step {
        const char* cmd;
        bool required;  // false: log-and-continue on failure/timeout, true: abort the rest
    };
    static const Step kSteps[] = {
        {"AT+EBOOT=1", false},
        {"AT+CMEE=1", false},           // verbose +CME ERROR: N instead of bare ERROR
        {"AT+CMER=1,0,0,2,0", false},   // enable unsolicited mobile-equipment-event reporting
        {"AT+ESIMS=1", false},          // SIM-state URCs on (confirmed real, task's one correct guess)
        {"AT+CSCS=\"UCS2\"", false},    // SMS/phonebook character set
        {"AT+EDSDA=1", false},          // dual-SIM-dual-active mode announce (harmless single-SIM)
        {"AT+ESLOTSINFO=1", false},     // slot-info URCs on
    };
    for (const auto& step : kSteps) {
        AtResult r = mCmdChannel.sendCommand(step.cmd, AtCommandType::kNoResult, "",
                                              AtChannel::kBootTimeoutMs);
        if (!r.ok) {
            ALOGW("boot handshake step '%s' failed/timed out%s", step.cmd,
                  cmeErrorNote(r).c_str());
            if (step.required) return;
        }
    }
    // AT+ICCID=1 and AT+EFUN=0 (radio starts powered off, matching the real RIL's lifecycle --
    // the framework brings it up explicitly via setRadioPower(true)) are deliberately NOT sent
    // here: AT+ICCID=1's exact URC-vs-response shape and AT+EFUN's argument semantics were not
    // instruction-by-instruction verified (unlike AT+ESIMS/CMER/CSCS, which are plain 3GPP/ETSI
    // commands with well-known semantics) -- sending an unverified proprietary command during
    // an unattended connect is exactly the kind of one-shot risk the fact log's method
    // discipline section warns against. Left for phase 2 once a live AT trace confirms them.
    ALOGI("boot handshake done");
}

// --------------------------------------------------------------------------------------------
// getIccCardStatus
// --------------------------------------------------------------------------------------------
::android::hardware::Return<void> MindoneRadio::getIccCardStatus(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("AT+CPIN?", AtCommandType::kSingleLine, "+CPIN");

    V1_0::CardStatus cs{};
    cs.gsmUmtsSubscriptionAppIndex = -1;
    cs.cdmaSubscriptionAppIndex = -1;
    cs.imsSubscriptionAppIndex = -1;

    if (!r.ok || r.intermediates.empty()) {
        cs.cardState = V1_0::CardState::ABSENT;
        cs.universalPinState = V1_0::PinState::UNKNOWN;
    } else {
        cs.cardState = V1_0::CardState::PRESENT;
        std::string line = r.intermediates.front();
        char* cur = const_cast<char*>(line.c_str());
        char* pinState = nullptr;
        V1_0::AppStatus app{};
        app.appType = V1_0::AppType::UNKNOWN;  // AT+CPIN? does not distinguish SIM vs USIM
        app.pin1 = V1_0::PinState::UNKNOWN;
        app.pin2 = V1_0::PinState::UNKNOWN;
        if (at_tok_start(&cur) == 0 && at_tok_nextstr(&cur, &pinState) == 0 && pinState) {
            std::string state = pinState;
            if (state == "READY") {
                app.appState = V1_0::AppState::READY;
                cs.universalPinState = V1_0::PinState::ENABLED_VERIFIED;
            } else if (state == "SIM PIN") {
                app.appState = V1_0::AppState::PIN;
                app.pin1 = V1_0::PinState::ENABLED_NOT_VERIFIED;
                cs.universalPinState = V1_0::PinState::ENABLED_NOT_VERIFIED;
            } else if (state == "SIM PUK") {
                app.appState = V1_0::AppState::PUK;
                app.pin1 = V1_0::PinState::ENABLED_BLOCKED;
                cs.universalPinState = V1_0::PinState::ENABLED_BLOCKED;
            } else {
                app.appState = V1_0::AppState::SUBSCRIPTION_PERSO;
                cs.universalPinState = V1_0::PinState::UNKNOWN;
            }
        } else {
            app.appState = V1_0::AppState::UNKNOWN;
        }
        cs.applications = hidl_vec<V1_0::AppStatus>{app};
        cs.gsmUmtsSubscriptionAppIndex = 0;
    }

    const auto info = mkInfo(serial, r.ok || cs.cardState == V1_0::CardState::ABSENT
                                          ? V1_0::RadioError::NONE
                                          : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->getIccCardStatusResponse(info, cs);
    } else if (mResponseV1_0) {
        mResponseV1_0->getIccCardStatusResponse(info, cs);
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// getSignalStrength / getSignalStrength_1_6
// --------------------------------------------------------------------------------------------
namespace {
constexpr uint32_t kInvalidU = 0x7fffffff;
constexpr int32_t kInvalidI = 0x7fffffff;

V1_0::GsmSignalStrength invalidGsm() { return {kInvalidU, kInvalidU, kInvalidI}; }
V1_0::CdmaSignalStrength invalidCdma() { return {kInvalidU, kInvalidU}; }
V1_0::EvdoSignalStrength invalidEvdo() { return {kInvalidU, kInvalidU, kInvalidU}; }

// Parses "+CSQ: <rssi>,<ber>" and converts to the 3GPP TS 27.007 asu scale GsmSignalStrength
// expects (0-31, 99 = unknown) -- AT+CSQ already reports on that exact scale, no conversion
// needed beyond the token split.
bool parseCsq(const std::string& line, V1_0::GsmSignalStrength* out) {
    char* cur = const_cast<char*>(line.c_str());
    int rssi, ber;
    if (at_tok_start(&cur) != 0) return false;
    if (!tokNextInt(&cur, &rssi)) return false;
    if (!tokNextInt(&cur, &ber)) ber = 99;
    out->signalStrength = static_cast<uint32_t>(rssi);
    out->bitErrorRate = static_cast<uint32_t>(ber);
    out->timingAdvance = kInvalidI;
    return true;
}
}  // namespace

::android::hardware::Return<void> MindoneRadio::getSignalStrength(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("AT+CSQ", AtCommandType::kSingleLine, "+CSQ");

    V1_0::SignalStrength ss{};
    ss.gw = invalidGsm();
    ss.cdma = invalidCdma();
    ss.evdo = invalidEvdo();
    ss.lte = V1_0::LteSignalStrength{kInvalidU, kInvalidU, kInvalidU,
                                      static_cast<int32_t>(kInvalidU), kInvalidU, kInvalidU};
    if (r.ok && !r.intermediates.empty()) parseCsq(r.intermediates.front(), &ss.gw);

    const auto info = mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->getSignalStrengthResponse(info, ss);
    } else if (mResponseV1_0) {
        mResponseV1_0->getSignalStrengthResponse(info, ss);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::getSignalStrength_1_6(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("AT+CSQ", AtCommandType::kSingleLine, "+CSQ");

    V1_6::SignalStrength ss{};
    ss.gsm = invalidGsm();
    ss.cdma = invalidCdma();
    ss.evdo = invalidEvdo();
    ss.lte = V1_6::LteSignalStrength{};  // {} = every field 0; real "invalid" values are phase 2
    ss.tdscdma = V1_2::TdscdmaSignalStrength{};
    ss.wcdma = V1_2::WcdmaSignalStrength{};
    ss.nr = V1_6::NrSignalStrength{};
    if (r.ok && !r.intermediates.empty()) parseCsq(r.intermediates.front(), &ss.gsm);

    if (mResponseV1_6) {
        const auto info = mkInfo16(serial, r.ok ? V1_6::RadioError::NONE
                                                 : V1_6::RadioError::MODEM_ERR);
        mResponseV1_6->getSignalStrengthResponse_1_6(info, ss);
    } else {
        ALOGE("getSignalStrength_1_6: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// getVoiceRegistrationState[_1_6] / getDataRegistrationState[_1_6]
// --------------------------------------------------------------------------------------------
MindoneRadio::RegState MindoneRadio::queryRegState(bool isData) {
    const char* cmd = isData ? "AT+CGREG?" : "AT+CREG?";
    const char* prefix = isData ? "+CGREG" : "+CREG";
    AtResult r = mCmdChannel.sendCommand(cmd, AtCommandType::kSingleLine, prefix);
    RegState out;
    if (!r.ok || r.intermediates.empty()) return out;
    char* cur = const_cast<char*>(r.intermediates.front().c_str());
    int n, stat;
    if (at_tok_start(&cur) != 0) return out;
    if (!tokNextInt(&cur, &n)) return out;   // <n> = unsolicited-result-code mode, ignored here
    if (!tokNextInt(&cur, &stat)) return out;
    out.regState = stat;  // +CREG/+CGREG <stat> values line up 1:1 with V1_0::RegState's own
                           // NOT_REG_MT_NOT_SEARCHING_OP(0)..REG_DENIED(3)/UNKNOWN(4)/ROAMING(5)
    out.rat = 0;  // RadioTechnology::UNKNOWN -- +CREG/+CGREG don't carry a RAT id; the MTK
                  // extension AT+ECGREG does (RIL-MINIMAL-1409 AT map) but is not
                  // queried in this phase-1 skeleton (see file header "scope").
    return out;
}

::android::hardware::Return<void> MindoneRadio::getVoiceRegistrationState(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    RegState rs = queryRegState(/*isData=*/false);
    V1_0::VoiceRegStateResult out{};
    out.regState = static_cast<V1_0::RegState>(rs.regState);
    out.rat = rs.rat;
    out.cssSupported = false;
    out.roamingIndicator = -1;
    out.systemIsInPrl = -1;
    out.defaultRoamingIndicator = -1;
    out.reasonForDenial = 0;
    const auto info = mkInfo(serial, V1_0::RadioError::NONE);
    if (mResponseV1_6) {
        mResponseV1_6->getVoiceRegistrationStateResponse(info, out);
    } else if (mResponseV1_0) {
        mResponseV1_0->getVoiceRegistrationStateResponse(info, out);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::getVoiceRegistrationState_1_6(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    RegState rs = queryRegState(/*isData=*/false);
    V1_6::RegStateResult out{};
    out.regState = static_cast<V1_0::RegState>(rs.regState);
    out.rat = static_cast<V1_4::RadioTechnology>(rs.rat);
    out.reasonForDenial = V1_5::RegistrationFailCause::NONE;
    // cellIdentity, registeredPlmn, accessTechnologySpecificInfo: left HIDL-default (empty
    // string / "noinit" safe-union variant) -- not yet parsed from AT+CREG's optional
    // <lac>,<ci>,<AcT> tail. Spec-legal; phase 2 should fill these in.
    if (mResponseV1_6) {
        const auto info = mkInfo16(serial, V1_6::RadioError::NONE);
        mResponseV1_6->getVoiceRegistrationStateResponse_1_6(info, out);
    } else {
        ALOGE("getVoiceRegistrationState_1_6: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::getDataRegistrationState(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    RegState rs = queryRegState(/*isData=*/true);
    V1_0::DataRegStateResult out{};
    out.regState = static_cast<V1_0::RegState>(rs.regState);
    out.rat = rs.rat;
    out.reasonDataDenied = -1;
    out.maxDataCalls = 1;
    const auto info = mkInfo(serial, V1_0::RadioError::NONE);
    if (mResponseV1_6) {
        mResponseV1_6->getDataRegistrationStateResponse(info, out);
    } else if (mResponseV1_0) {
        mResponseV1_0->getDataRegistrationStateResponse(info, out);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::getDataRegistrationState_1_6(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    RegState rs = queryRegState(/*isData=*/true);
    V1_6::RegStateResult out{};
    out.regState = static_cast<V1_0::RegState>(rs.regState);
    out.rat = static_cast<V1_4::RadioTechnology>(rs.rat);
    out.reasonForDenial = V1_5::RegistrationFailCause::NONE;
    if (mResponseV1_6) {
        const auto info = mkInfo16(serial, V1_6::RadioError::NONE);
        mResponseV1_6->getDataRegistrationStateResponse_1_6(info, out);
    } else {
        ALOGE("getDataRegistrationState_1_6: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// getOperator
// --------------------------------------------------------------------------------------------
::android::hardware::Return<void> MindoneRadio::getOperator(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("AT+COPS?", AtCommandType::kSingleLine, "+COPS");
    hidl_string name;
    if (r.ok && !r.intermediates.empty()) {
        char* cur = const_cast<char*>(r.intermediates.front().c_str());
        int mode, format;
        char* oper = nullptr;
        if (at_tok_start(&cur) == 0 && tokNextInt(&cur, &mode) && tokNextInt(&cur, &format) &&
            at_tok_nextstr(&cur, &oper) == 0 && oper) {
            name = oper;
        }
    }
    // Same string for long/short/numeric -- see file header "scope".
    const auto info = mkInfo(serial, V1_0::RadioError::NONE);
    if (mResponseV1_6) {
        mResponseV1_6->getOperatorResponse(info, name, name, name);
    } else if (mResponseV1_0) {
        mResponseV1_0->getOperatorResponse(info, name, name, name);
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// dial / hangup / getCurrentCalls[_1_6] / acceptCall
// --------------------------------------------------------------------------------------------
::android::hardware::Return<void> MindoneRadio::dial(int32_t serial, const V1_0::Dial& dialInfo) {
    std::lock_guard<std::mutex> lk(mAtLock);
    std::string cmd = "ATD" + std::string(dialInfo.address) + ";";
    AtResult r = mCmdChannel.sendCommand(cmd, AtCommandType::kNoResult);
    const auto info =
        mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->dialResponse(info);
    } else if (mResponseV1_0) {
        mResponseV1_0->dialResponse(info);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::hangup(int32_t serial, int32_t gsmIndex) {
    std::lock_guard<std::mutex> lk(mAtLock);
    // 3GPP TS 27.007 +CHLD=1X releases a specific active call X and accepts any other
    // held/waiting call -- the closest standard primitive to "hang up call #gsmIndex".
    std::string cmd = "AT+CHLD=1" + std::to_string(gsmIndex);
    AtResult r = mCmdChannel.sendCommand(cmd, AtCommandType::kNoResult);
    const auto info =
        mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->hangupConnectionResponse(info);
    } else if (mResponseV1_0) {
        mResponseV1_0->hangupConnectionResponse(info);
    }
    return Void();
}

namespace {
// Parses one "+CLCC: <id>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>]]" line into a
// V1_0::Call. 3GPP TS 27.007 7.18.
bool parseClccLine(const std::string& line, V1_0::Call* out) {
    char* cur = const_cast<char*>(line.c_str());
    int id, dir, stat, mode, mpty;
    if (at_tok_start(&cur) != 0) return false;
    if (!tokNextInt(&cur, &id)) return false;
    if (!tokNextInt(&cur, &dir)) return false;
    if (!tokNextInt(&cur, &stat)) return false;
    if (!tokNextInt(&cur, &mode)) return false;
    if (!tokNextInt(&cur, &mpty)) return false;
    out->index = id;
    out->state = static_cast<V1_0::CallState>(stat);  // 3GPP stat values line up with CallState
    out->isMpty = mpty != 0;
    out->isMT = dir != 0;
    out->isVoice = (mode == 0);
    out->toa = 129;
    out->numberPresentation = V1_0::CallPresentation::ALLOWED;
    out->namePresentation = V1_0::CallPresentation::ALLOWED;
    if (at_tok_hasmore(&cur)) {
        int type;
        char* number = nullptr;
        if (at_tok_nextstr(&cur, &number) == 0 && number) out->number = number;
        if (tokNextInt(&cur, &type)) out->toa = type;
    }
    return true;
}
}  // namespace

::android::hardware::Return<void> MindoneRadio::getCurrentCalls(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("AT+CLCC", AtCommandType::kMultiLine, "+CLCC");
    std::vector<V1_0::Call> calls;
    for (const auto& line : r.intermediates) {
        V1_0::Call c{};
        if (parseClccLine(line, &c)) calls.push_back(c);
    }
    const auto info = mkInfo(serial, V1_0::RadioError::NONE);
    if (mResponseV1_6) {
        mResponseV1_6->getCurrentCallsResponse(info, calls);
    } else if (mResponseV1_0) {
        mResponseV1_0->getCurrentCallsResponse(info, calls);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::getCurrentCalls_1_6(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("AT+CLCC", AtCommandType::kMultiLine, "+CLCC");
    std::vector<V1_6::Call> calls;
    for (const auto& line : r.intermediates) {
        V1_0::Call c{};
        if (!parseClccLine(line, &c)) continue;
        V1_6::Call c16{};
        c16.base.base = c;
        c16.base.audioQuality = V1_2::AudioQuality::UNSPECIFIED;
        calls.push_back(c16);
    }
    if (mResponseV1_6) {
        const auto info = mkInfo16(serial, V1_6::RadioError::NONE);
        mResponseV1_6->getCurrentCallsResponse_1_6(info, calls);
    } else {
        ALOGE("getCurrentCalls_1_6: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::acceptCall(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand("ATA", AtCommandType::kNoResult);
    const auto info =
        mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->acceptCallResponse(info);
    } else if (mResponseV1_0) {
        mResponseV1_0->acceptCallResponse(info);
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// sendSms
// --------------------------------------------------------------------------------------------
::android::hardware::Return<void> MindoneRadio::sendSms(int32_t serial,
                                                          const V1_0::GsmSmsMessage& message) {
    std::lock_guard<std::mutex> lk(mAtLock);
    // TP-layer length in octets, per 3GPP TS 27.005 4.3 -- the PDU minus the SMSC prefix, which
    // is either absent (single "00" byte meaning "use stored SMSC") or the smscPdu field; we
    // always send "00" (use stored SMSC), matching most reference-ril-derived implementations'
    // simplest path, and note the length is (pdu.size()/2 - 1) octets (the leading "00").
    const std::string& pdu = message.pdu;
    int pduLenOctets = static_cast<int>(pdu.size() / 2) - 1;
    std::string cmd = "AT+CMGS=" + std::to_string(pduLenOctets);
    AtResult r = mCmdChannel.sendSmsCommand(cmd, pdu, "+CMGS");

    V1_0::SendSmsResult sms{};
    sms.messageRef = -1;
    sms.errorCode = -1;
    if (r.ok && !r.intermediates.empty()) {
        char* cur = const_cast<char*>(r.intermediates.front().c_str());
        int mr;
        if (at_tok_start(&cur) == 0 && tokNextInt(&cur, &mr)) sms.messageRef = mr;
    }
    const auto info =
        mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::GENERIC_FAILURE);
    if (mResponseV1_6) {
        mResponseV1_6->sendSmsResponse(info, sms);
    } else if (mResponseV1_0) {
        mResponseV1_0->sendSmsResponse(info, sms);
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// setupDataCall[_1_2/_1_4/_1_5/_1_6] / deactivateDataCall[_1_2]
// --------------------------------------------------------------------------------------------
V1_6::SetupDataCallResult MindoneRadio::doSetupDataCall(int32_t cid, const std::string& apn,
                                                          const std::string& protocol) {
    // See file header "scope": fixed cid, IPv4 default bearer, no auth, best-effort ifname.
    std::string cgdcont =
        "AT+CGDCONT=" + std::to_string(cid) + ",\"" + protocol + "\",\"" + apn + "\"";
    AtResult r1 = mCmdChannel.sendCommand(cgdcont, AtCommandType::kNoResult);
    AtResult r2 = mCmdChannel.sendCommand("AT+CGACT=1," + std::to_string(cid),
                                           AtCommandType::kNoResult, "", AtChannel::kBootTimeoutMs);

    V1_6::SetupDataCallResult out{};
    out.cid = cid;
    out.suggestedRetryTime = -1;
    out.mtuV4 = 1500;
    out.mtuV6 = 1500;
    (void)protocol;  // fixed to IP for this phase-1 skeleton, see file header "scope"
    if (r1.ok && r2.ok) {
        out.cause = V1_6::DataCallFailCause::NONE;
        out.active = V1_4::DataConnActiveStatus::ACTIVE;
        out.type = V1_4::PdpProtocolType::IP;
        // Best-effort placeholder -- NOT confirmed against a live data call, see file header.
        out.ifname = "ccmni0";
    } else {
        out.cause = V1_6::DataCallFailCause::ERROR_UNSPECIFIED;
        out.active = V1_4::DataConnActiveStatus::INACTIVE;
        ALOGW("doSetupDataCall(%s): CGDCONT %s, CGACT %s", apn.c_str(),
              r1.ok ? "ok" : "failed", r2.ok ? "ok" : "failed");
    }
    return out;
}

bool MindoneRadio::doDeactivateDataCall(int32_t cid) {
    AtResult r = mCmdChannel.sendCommand("AT+CGACT=0," + std::to_string(cid),
                                          AtCommandType::kNoResult, "", AtChannel::kBootTimeoutMs);
    return r.ok;
}

namespace {
// V1_6::SetupDataCallResult.type/active are enums (V1_4::PdpProtocolType/DataConnActiveStatus);
// V1_0::SetupDataCallResult.type is a plain hidl_string and its mtu is a single field (mtuV4/V6
// only exist from 1.5 on) -- hence the fixed "IP" literal and the mtuV4 source below, not a
// direct field copy.
V1_0::SetupDataCallResult downcastToV0(const V1_6::SetupDataCallResult& r6) {
    V1_0::SetupDataCallResult r0{};
    r0.status = static_cast<V1_0::DataCallFailCause>(static_cast<int32_t>(r6.cause));
    r0.suggestedRetryTime = static_cast<int32_t>(r6.suggestedRetryTime);
    r0.cid = r6.cid;
    r0.active = static_cast<int32_t>(r6.active);
    r0.type = "IP";
    r0.ifname = r6.ifname;
    // addresses/dnses/gateways/pcscf are comma-joined strings pre-1.4; left empty here since
    // this skeleton does not yet run DHCP/parse +CGCONTRDP for the assigned address (phase 2).
    r0.mtu = r6.mtuV4;
    return r0;
}
V1_4::SetupDataCallResult toV4(const V1_6::SetupDataCallResult& r6) {
    V1_4::SetupDataCallResult r4{};
    r4.cause = static_cast<V1_4::DataCallFailCause>(static_cast<int32_t>(r6.cause));
    r4.suggestedRetryTime = r6.suggestedRetryTime;
    r4.cid = r6.cid;
    r4.active = r6.active;
    r4.type = r6.type;
    r4.ifname = r6.ifname;
    r4.mtu = r6.mtuV4;
    return r4;
}
V1_5::SetupDataCallResult toV5(const V1_6::SetupDataCallResult& r6) {
    V1_5::SetupDataCallResult r5{};
    r5.cause = static_cast<V1_4::DataCallFailCause>(static_cast<int32_t>(r6.cause));
    r5.suggestedRetryTime = r6.suggestedRetryTime;
    r5.cid = r6.cid;
    r5.active = r6.active;
    r5.type = r6.type;
    r5.ifname = r6.ifname;
    r5.mtuV4 = r6.mtuV4;
    r5.mtuV6 = r6.mtuV6;
    return r5;
}
}  // namespace

::android::hardware::Return<void> MindoneRadio::setupDataCall(
    int32_t serial, V1_0::RadioTechnology /*radioTechnology*/,
    const V1_0::DataProfileInfo& dataProfileInfo, bool /*modemCognitive*/,
    bool /*roamingAllowed*/, bool /*isRoaming*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    V1_6::SetupDataCallResult r6 = doSetupDataCall(1, dataProfileInfo.apn, "IP");
    const auto info = mkInfo(serial, r6.cause == V1_6::DataCallFailCause::NONE
                                          ? V1_0::RadioError::NONE
                                          : V1_0::RadioError::GENERIC_FAILURE);
    V1_0::SetupDataCallResult r0 = downcastToV0(r6);
    if (mResponseV1_6) {
        mResponseV1_6->setupDataCallResponse(info, r0);
    } else if (mResponseV1_0) {
        mResponseV1_0->setupDataCallResponse(info, r0);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::setupDataCall_1_2(
    int32_t serial, V1_2::AccessNetwork /*accessNetwork*/,
    const V1_0::DataProfileInfo& dataProfileInfo, bool /*modemCognitive*/, bool /*roamingAllowed*/,
    bool /*isRoaming*/, V1_2::DataRequestReason /*reason*/,
    const hidl_vec<hidl_string>& /*addresses*/, const hidl_vec<hidl_string>& /*dnses*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    V1_6::SetupDataCallResult r6 = doSetupDataCall(1, dataProfileInfo.apn, "IP");
    const auto info = mkInfo(serial, r6.cause == V1_6::DataCallFailCause::NONE
                                          ? V1_0::RadioError::NONE
                                          : V1_0::RadioError::GENERIC_FAILURE);
    V1_0::SetupDataCallResult r0 = downcastToV0(r6);
    if (mResponseV1_6) {
        mResponseV1_6->setupDataCallResponse(info, r0);
    } else if (mResponseV1_0) {
        mResponseV1_0->setupDataCallResponse(info, r0);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::setupDataCall_1_4(
    int32_t serial, V1_4::AccessNetwork /*accessNetwork*/,
    const V1_4::DataProfileInfo& dataProfileInfo, bool /*roamingAllowed*/,
    V1_2::DataRequestReason /*reason*/, const hidl_vec<hidl_string>& /*addresses*/,
    const hidl_vec<hidl_string>& /*dnses*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    V1_6::SetupDataCallResult r6 = doSetupDataCall(1, dataProfileInfo.apn, "IP");
    // setupDataCallResponse_1_4 is declared starting at IRadioResponse@1.4 -- only reachable via
    // mResponseV1_6 (our only cached pointer above 1.0), never a plain V1_0 fallback (confirmed
    // by synchk-ril.sh: calling it through mResponseV1_0 does not compile).
    if (mResponseV1_6) {
        const auto info = mkInfo(serial, r6.cause == V1_6::DataCallFailCause::NONE
                                              ? V1_0::RadioError::NONE
                                              : V1_0::RadioError::GENERIC_FAILURE);
        mResponseV1_6->setupDataCallResponse_1_4(info, toV4(r6));
    } else {
        ALOGE("setupDataCall_1_4: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::setupDataCall_1_5(
    int32_t serial, V1_5::AccessNetwork /*accessNetwork*/,
    const V1_5::DataProfileInfo& dataProfileInfo, bool /*roamingAllowed*/,
    V1_2::DataRequestReason /*reason*/, const hidl_vec<V1_5::LinkAddress>& /*addresses*/,
    const hidl_vec<hidl_string>& /*dnses*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    V1_6::SetupDataCallResult r6 = doSetupDataCall(1, dataProfileInfo.apn, "IP");
    // setupDataCallResponse_1_5 is declared starting at IRadioResponse@1.5 -- V1_6-pointer only,
    // same reasoning as setupDataCall_1_4 above.
    if (mResponseV1_6) {
        const auto info = mkInfo(serial, r6.cause == V1_6::DataCallFailCause::NONE
                                              ? V1_0::RadioError::NONE
                                              : V1_0::RadioError::GENERIC_FAILURE);
        mResponseV1_6->setupDataCallResponse_1_5(info, toV5(r6));
    } else {
        ALOGE("setupDataCall_1_5: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::setupDataCall_1_6(
    int32_t serial, V1_5::AccessNetwork /*accessNetwork*/,
    const V1_5::DataProfileInfo& dataProfileInfo, bool /*roamingAllowed*/,
    V1_2::DataRequestReason /*reason*/, const hidl_vec<V1_5::LinkAddress>& /*addresses*/,
    const hidl_vec<hidl_string>& /*dnses*/, int32_t /*pduSessionId*/,
    const V1_6::OptionalSliceInfo& /*sliceInfo*/,
    const V1_6::OptionalTrafficDescriptor& /*trafficDescriptor*/, bool /*matchAllRuleAllowed*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    V1_6::SetupDataCallResult r6 = doSetupDataCall(1, dataProfileInfo.apn, "IP");
    if (mResponseV1_6) {
        const auto info = mkInfo16(serial, r6.cause == V1_6::DataCallFailCause::NONE
                                                ? V1_6::RadioError::NONE
                                                : V1_6::RadioError::GENERIC_FAILURE);
        mResponseV1_6->setupDataCallResponse_1_6(info, r6);
    } else {
        ALOGE("setupDataCall_1_6: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::deactivateDataCall(int32_t serial, int32_t cid,
                                                                     bool /*reasonRadioShutDown*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    bool ok = doDeactivateDataCall(cid);
    const auto info = mkInfo(serial, ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->deactivateDataCallResponse(info);
    } else if (mResponseV1_0) {
        mResponseV1_0->deactivateDataCallResponse(info);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::deactivateDataCall_1_2(
    int32_t serial, int32_t cid, V1_2::DataRequestReason /*reason*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    bool ok = doDeactivateDataCall(cid);
    const auto info = mkInfo(serial, ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->deactivateDataCallResponse(info);
    } else if (mResponseV1_0) {
        mResponseV1_0->deactivateDataCallResponse(info);
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// setRadioPower / setRadioPower_1_5 / setRadioPower_1_6
// --------------------------------------------------------------------------------------------
// Idle power saving the modem supports and the stock stack never asks for (F4486).
//
// Surveyed the modem firmware's own AT table (488 vendor commands in MOLY.LR13.R2.MP.V195) and
// checked each candidate against the company it keeps in that table, because the name alone
// misleads:
//   +EUEDRX: (7),(0-65535)  -- REAL. Sits among the NAS/registration commands (+CSCON, +EDRAT,
//                              +EREGINFO), i.e. it is UE eDRX: longer idle gaps between paging
//                              checks. No stock vendor library on the device references it --
//                              grep over /vendor/lib*, /vendor/bin/hw finds nothing -- so the
//                              modem has always run on its default DRX.
//   +EPSMAP                 -- NOT power saving despite the name. It sits with +ESIMMAP and the
//                              PDP-context set (+CGDCONT, +PSBEARER, +CGTFT): a PS APN map.
//   +ESLP: (0,1)            -- factory/test set, immediate neighbours are +EGMR (IMEI write),
//                              +ERFTX, +EADC. Not touched.
//   +EWOCFGSET/+EWOKEEPALIVE-- Wi-Fi-calling (ePDG) keepalives, next to +EWIFIEN/+EEPDG. Not a
//                              general TCP-keepalive offload.
// So exactly one lever is both real and safe, and it is the one applied here.
//
// 🔴 Honest limit: the second parameter's 0-65535 encoding is not decoded. The default below is
// the smallest non-zero setting deliberately -- it buys a real idle gap while keeping paging
// delay at its minimum, which matters because eDRX is a trade: longer sleep, later incoming
// calls and SMS. The value is overridable, "0" disables, and the applied setting is always read
// back and logged so the encoding can be confirmed from one boot log rather than assumed.
void MindoneRadio::applyPowerSavingProfileLocked() {
    char prop[PROPERTY_VALUE_MAX] = {0};

    if (mPowerSavingApplied) return;
    mPowerSavingApplied = true;

    property_get("persist.vendor.mindone.edrx", prop, "2");
    if (prop[0] == '0' && prop[1] == '\0') {
        ALOGI("power saving: eDRX disabled by persist.vendor.mindone.edrx=0");
        return;
    }

    AtResult sup = mCmdChannel.sendCommand("AT+EUEDRX=?", AtCommandType::kSingleLine, "+EUEDRX:");
    if (!sup.ok) {
        ALOGI("power saving: modem does not accept AT+EUEDRX=? (%s) -- leaving DRX alone",
              sup.finalLine.c_str());
        return;
    }
    ALOGI("power saving: eDRX supported, modem reports %s",
          sup.intermediates.empty() ? "(no detail)" : sup.intermediates.front().c_str());

    const std::string cmd = std::string("AT+EUEDRX=7,") + prop;
    AtResult set = mCmdChannel.sendCommand(cmd, AtCommandType::kNoResult);
    if (!set.ok) {
        ALOGI("power saving: %s rejected (%s) -- modem stays on default DRX", cmd.c_str(),
              set.finalLine.c_str());
        return;
    }

    AtResult rd = mCmdChannel.sendCommand("AT+EUEDRX?", AtCommandType::kSingleLine, "+EUEDRX:");
    ALOGI("power saving: applied %s; modem now reports %s", cmd.c_str(),
          (rd.ok && !rd.intermediates.empty()) ? rd.intermediates.front().c_str()
                                               : "(read-back failed)");
}

::android::hardware::Return<void> MindoneRadio::setRadioPower(int32_t serial, bool on) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand(on ? "AT+CFUN=1" : "AT+CFUN=0",
                                          AtCommandType::kNoResult, "", AtChannel::kBootTimeoutMs);
    if (r.ok) mRadioPowerOn = on;
    if (r.ok && on) applyPowerSavingProfileLocked();
    const auto info =
        mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->setRadioPowerResponse(info);
    } else if (mResponseV1_0) {
        mResponseV1_0->setRadioPowerResponse(info);
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::setRadioPower_1_5(
    int32_t serial, bool powerOn, bool /*forEmergencyCall*/, bool /*preferredForEmergencyCall*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand(powerOn ? "AT+CFUN=1" : "AT+CFUN=0",
                                          AtCommandType::kNoResult, "", AtChannel::kBootTimeoutMs);
    if (r.ok) mRadioPowerOn = powerOn;
    // setRadioPowerResponse_1_5 is declared starting at IRadioResponse@1.5 -- V1_6-pointer only.
    if (mResponseV1_6) {
        const auto info =
            mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
        mResponseV1_6->setRadioPowerResponse_1_5(info);
    } else {
        ALOGE("setRadioPower_1_5: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

::android::hardware::Return<void> MindoneRadio::setRadioPower_1_6(
    int32_t serial, bool powerOn, bool /*forEmergencyCall*/, bool /*preferredForEmergencyCall*/) {
    std::lock_guard<std::mutex> lk(mAtLock);
    AtResult r = mCmdChannel.sendCommand(powerOn ? "AT+CFUN=1" : "AT+CFUN=0",
                                          AtCommandType::kNoResult, "", AtChannel::kBootTimeoutMs);
    if (r.ok) mRadioPowerOn = powerOn;
    if (mResponseV1_6) {
        const auto info = mkInfo16(serial, r.ok ? V1_6::RadioError::NONE
                                                 : V1_6::RadioError::MODEM_ERR);
        mResponseV1_6->setRadioPowerResponse_1_6(info);
    } else {
        ALOGE("setRadioPower_1_6: no 1.6 IRadioResponse bound, cannot answer");
    }
    return Void();
}

// --------------------------------------------------------------------------------------------
// getDeviceIdentity
// --------------------------------------------------------------------------------------------
::android::hardware::Return<void> MindoneRadio::getDeviceIdentity(int32_t serial) {
    std::lock_guard<std::mutex> lk(mAtLock);
    // Plain AT+CGSN, matching what RmcOemRequestHandler::requestGetImei() itself sends (recon
    // finding, see modem/ril/AT-MAP-NOTES.md "surprises" -- the class mixes this
    // standard command with the proprietary AT+EGMR family for other identity fields, not used
    // here). IMEISV/ESN/MEID are left empty -- see file header "scope".
    AtResult r = mCmdChannel.sendCommand("AT+CGSN", AtCommandType::kNumeric);
    hidl_string imei;
    if (r.ok && !r.intermediates.empty()) imei = r.intermediates.front();
    const auto info = mkInfo(serial, r.ok ? V1_0::RadioError::NONE : V1_0::RadioError::MODEM_ERR);
    if (mResponseV1_6) {
        mResponseV1_6->getDeviceIdentityResponse(info, imei, "", "", "");
    } else if (mResponseV1_0) {
        mResponseV1_0->getDeviceIdentityResponse(info, imei, "", "", "");
    }
    return Void();
}

}  // namespace mindone::ril
