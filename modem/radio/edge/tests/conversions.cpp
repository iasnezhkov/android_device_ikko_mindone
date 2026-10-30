/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "Conversions.h"
#include "Validation.h"
#include <cassert>
#include <iostream>
namespace c = mindone::radio::convert;
namespace v = mindone::radio::validation;
namespace r = aidl::android::hardware::radio;
mr_string bytes(const char* value) { return {reinterpret_cast<const uint8_t*>(value), static_cast<uint32_t>(std::char_traits<char>::length(value))}; }
int main() {
    assert(v::plmn("44010") && v::plmn("310260") && v::plmn("") && !v::plmn("4401x"));
    assert(v::thresholds({}, 0, 0, INT32_MAX));
    assert(v::thresholds({-100,-90}, 9, -113, -51));
    assert(!v::thresholds({-100,-90}, 10, -113, -51));
    assert(!v::thresholds({-90,-100}, 0, -113, -51));
    assert(!v::thresholds({1,1}, 0, 0, 9));
    assert(!v::signalCriteria(2, 1, 0, 0, {-100}));
    assert(v::signalCriteria(1, 1, 0, 0, {-113,-51}));
    assert(!v::signalCriteria(1, 1, -1, 0, {}));
    assert(!v::signalCriteria(3, 3, 0, -1, {}));
    assert(!v::signalCriteria(3, 3, 0, 0, {-141}));
    assert(v::signalCriteria(6, 8, 0, 0, {-23,40}));
    std::string text;
    assert(c::text({}, text) && text.empty());
    assert(!c::text({nullptr, 1}, text));
    const uint8_t embedded[] = {'a',0,'b'};
    assert(c::text({embedded,3},text) && text.size()==3 && text[1]==0);
    bool flag;
    assert(!c::boolean(2,flag) && c::boolean(1,flag) && flag);
    assert(c::selection(MR_SELECTION_MODE_MANUAL,flag) && flag);
    assert(!c::selection(MR_SELECTION_MODE_DEREGISTERED_UNVERIFIED,flag));
    r::network::SignalStrength signal;
    mr_signal_strength raw{};
    assert(c::signal(raw,signal));
    assert(signal.gsm.signalStrength==INT32_MAX && signal.lte.rsrp==INT32_MAX && signal.nr.timingAdvance==INT32_MAX);
    raw.lte_present=1; raw.lte.rsrp_present=1; raw.lte.rsrp=95;
    assert(c::signal(raw,signal) && signal.lte.rsrp==95 && signal.lte.rsrq==INT32_MAX);
    r::network::RegStateResult reg;
    mr_reg_state_result source{}; source.reg_state=MR_REG_STATE_UNKNOWN;
    assert(c::registration(source,reg));
    assert(reg.cellIdentity.getTag()==r::network::CellIdentity::Tag::noinit);
    assert(reg.accessTechnologySpecificInfo.getTag()==r::network::AccessTechnologySpecificInfo::Tag::noinit);
    source.reg_state=MR_REG_STATE_HOME;
    assert(!c::registration(source,reg));
    source.rat=MR_RAT_LTE; source.registered_plmn=bytes("44010");
    assert(c::registration(source,reg) && reg.registeredPlmn=="44010");
    mr_cell_info cell{}; cell.registered=1; cell.identity={MR_CELL_LTE,23,42,bytes("44010")};
    mr_value value{}; value.cells_data=&cell; value.cells_len=1;
    std::vector<r::network::CellInfo> cells;
    assert(c::cells(value,cells) && cells.size()==1);
    const auto& lte=cells[0].ratSpecificInfo.get<r::network::CellInfoRatSpecificInfo::Tag::lte>().cellIdentityLte;
    assert(lte.ci==42 && lte.tac==23 && lte.pci==INT32_MAX && lte.earfcn==INT32_MAX && lte.bandwidth==INT32_MAX);
    assert(lte.mcc=="440" && lte.mnc=="10" && lte.operatorNames.alphaLong.empty() && !lte.csgInfo);
    assert(static_cast<int32_t>(cells[0].connectionStatus)==INT32_MAX);
    mr_card_status card{}; card.gsm_umts_subscription_app_index=-1; card.cdma_subscription_app_index=-1; card.ims_subscription_app_index=-1;
    r::sim::CardStatus cardOut;
    assert(c::card(card,cardOut) && cardOut.applications.empty() && cardOut.gsmUmtsSubscriptionAppIndex==-1 && static_cast<int32_t>(cardOut.universalPinState)==0);
    card.applications_len=9; assert(!c::card(card,cardOut)); card.applications_len=0;
    mr_app_status app{2,5,0,bytes("A000"),bytes("USIM"),1,2,0};
    card.card_state=1; card.applications_data=&app; card.applications_len=1; card.gsm_umts_subscription_app_index=0;
    card.atr=bytes("3B00"); card.iccid=bytes("123"); card.eid=bytes("456"); card.slot_map={1,2}; card.supported_mep_mode=3;
    assert(c::card(card,cardOut) && cardOut.applications.size()==1 && cardOut.applications[0].aidPtr=="A000" && cardOut.applications[0].appLabelPtr=="USIM" && cardOut.applications[0].pin1Replaced);
    assert(cardOut.atr=="3B00" && cardOut.iccid=="123" && cardOut.eid=="456" && cardOut.slotMap.physicalSlotId==1 && cardOut.slotMap.portId==2 && static_cast<int32_t>(cardOut.supportedMepMode)==3);
    card.gsm_umts_subscription_app_index=1; assert(!c::card(card,cardOut)); card.gsm_umts_subscription_app_index=0;
    app.pin1=6; assert(!c::card(card,cardOut)); app.pin1=2;
    mr_radio_capability cap{9,4,16,bytes("modem0"),1};
    r::modem::RadioCapability capOut;
    assert(c::capability(cap,capOut) && capOut.session==9 && capOut.phase==4 && capOut.raf==16 && capOut.logicalModemUuid=="modem0" && capOut.status==1);
    cap.phase=5; assert(!c::capability(cap,capOut));
    std::optional<r::modem::ImeiInfo> imei;
    assert(c::imei({1,bytes("123456789012345"),bytes("01")},imei) && imei && imei->svn=="01" && static_cast<int32_t>(imei->type)==1);
    assert(!c::imei({3,{},{}},imei));
    mr_activity_stats_tech_specific_info tech{}; tech.rat=3; tech.txm_mode_time_ms[2]=17; tech.rx_mode_time_ms=21;
    r::modem::ActivityStatsInfo activity;
    assert(c::activity({4,5,&tech,1},activity) && activity.sleepModeTimeMs==4 && activity.idleModeTimeMs==5 && activity.techSpecificInfo[0].txmModetimeMs.size()==5 && activity.techSpecificInfo[0].txmModetimeMs[2]==17 && activity.techSpecificInfo[0].frequencyRange==0 && activity.techSpecificInfo[0].rxModeTimeMs==21);
    tech.rx_mode_time_ms=-1; assert(!c::activity({4,5,&tech,1},activity));
    int8_t modem=0;
    mr_phone_capability phone{-1,-1,1,&modem,1,-1};
    r::config::PhoneCapability phoneOut;
    assert(c::phone(phone,phoneOut) && phoneOut.maxActiveVoice==-1 && phoneOut.maxActiveData==-1 && phoneOut.maxActiveInternetData==-1 && phoneOut.isInternetLingeringSupported && phoneOut.logicalModemIds.size()==1);
    phone.max_active_data=-2; assert(!c::phone(phone,phoneOut));
    mr_sim_port_info port{bytes("iccid"),-1,0};
    mr_sim_slot_status slot{0,bytes("atr"),bytes("eid"),&port,1,0};
    value.slots_data=&slot; value.slots_len=1;
    std::vector<r::config::SimSlotStatus> slots;
    assert(c::slots(value,slots) && slots[0].portInfo[0].logicalSlotId==-1 && !slots[0].portInfo[0].portActive && slots[0].atr=="atr" && slots[0].eid=="eid");
    slot.port_info_len=0; assert(!c::slots(value,slots)); slot.port_info_len=1;
    port.port_active=1; assert(!c::slots(value,slots)); port.logical_slot_id=0;
    assert(c::slots(value,slots) && slots[0].portInfo[0].portActive);
    mr_uus_info uus{0,1,bytes("hello")};
    mr_call call{}; call.index=1; call.number=bytes("12345"); call.name=bytes("name"); call.is_voice=1; call.forwarded_number=bytes("678"); call.uus_info_data=&uus; call.uus_info_len=1; call.audio_quality=1;
    value.calls_data=&call; value.calls_len=1;
    std::vector<r::voice::Call> calls;
    assert(c::calls(value,calls) && calls[0].number=="12345" && calls[0].name=="name" && calls[0].isVoice && calls[0].forwardedNumber=="678" && calls[0].uusInfo[0].uusData=="hello" && static_cast<int32_t>(calls[0].audioQuality)==1);
    assert(c::calls(value,calls) && calls.size()==1);
    assert(v::tty(0) && v::tty(3) && !v::tty(4));
    std::cout << "all domain validation/conversion assertions passed\n";
}
