/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#include "../mindone_radio.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
int main(void) {
assert(mr_abi_version() == MR_ABI_VERSION);
assert(mr_layout_count() == 30);
{ mr_layout layout; assert(mr_layout_at(0, &layout) == MR_OK); assert(layout.name.len == strlen("mr_string")); assert(memcmp(layout.name.data,"mr_string",layout.name.len)==0); assert(layout.size == sizeof(mr_string)); assert(layout.alignment == _Alignof(mr_string)); assert(layout.offsets_len == 2);
assert(layout.offsets_data[0] == offsetof(mr_string, data));
assert(layout.offsets_data[1] == offsetof(mr_string, len));
}
{ mr_layout layout; assert(mr_layout_at(1, &layout) == MR_OK); assert(layout.name.len == strlen("mr_cell_identity")); assert(memcmp(layout.name.data,"mr_cell_identity",layout.name.len)==0); assert(layout.size == sizeof(mr_cell_identity)); assert(layout.alignment == _Alignof(mr_cell_identity)); assert(layout.offsets_len == 4);
assert(layout.offsets_data[0] == offsetof(mr_cell_identity, kind));
assert(layout.offsets_data[1] == offsetof(mr_cell_identity, area));
assert(layout.offsets_data[2] == offsetof(mr_cell_identity, cell));
assert(layout.offsets_data[3] == offsetof(mr_cell_identity, plmn));
}
{ mr_layout layout; assert(mr_layout_at(2, &layout) == MR_OK); assert(layout.name.len == strlen("mr_gsm_signal")); assert(memcmp(layout.name.data,"mr_gsm_signal",layout.name.len)==0); assert(layout.size == sizeof(mr_gsm_signal)); assert(layout.alignment == _Alignof(mr_gsm_signal)); assert(layout.offsets_len == 4);
assert(layout.offsets_data[0] == offsetof(mr_gsm_signal, signal_strength_present));
assert(layout.offsets_data[1] == offsetof(mr_gsm_signal, signal_strength));
assert(layout.offsets_data[2] == offsetof(mr_gsm_signal, bit_error_rate_present));
assert(layout.offsets_data[3] == offsetof(mr_gsm_signal, bit_error_rate));
}
{ mr_layout layout; assert(mr_layout_at(3, &layout) == MR_OK); assert(layout.name.len == strlen("mr_lte_signal")); assert(memcmp(layout.name.data,"mr_lte_signal",layout.name.len)==0); assert(layout.size == sizeof(mr_lte_signal)); assert(layout.alignment == _Alignof(mr_lte_signal)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_lte_signal, rsrp_present));
assert(layout.offsets_data[1] == offsetof(mr_lte_signal, rsrp));
assert(layout.offsets_data[2] == offsetof(mr_lte_signal, rsrq_present));
assert(layout.offsets_data[3] == offsetof(mr_lte_signal, rsrq));
assert(layout.offsets_data[4] == offsetof(mr_lte_signal, rssnr_present));
assert(layout.offsets_data[5] == offsetof(mr_lte_signal, rssnr));
}
{ mr_layout layout; assert(mr_layout_at(4, &layout) == MR_OK); assert(layout.name.len == strlen("mr_wcdma_signal")); assert(memcmp(layout.name.data,"mr_wcdma_signal",layout.name.len)==0); assert(layout.size == sizeof(mr_wcdma_signal)); assert(layout.alignment == _Alignof(mr_wcdma_signal)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_wcdma_signal, signal_strength_present));
assert(layout.offsets_data[1] == offsetof(mr_wcdma_signal, signal_strength));
assert(layout.offsets_data[2] == offsetof(mr_wcdma_signal, rscp_present));
assert(layout.offsets_data[3] == offsetof(mr_wcdma_signal, rscp));
assert(layout.offsets_data[4] == offsetof(mr_wcdma_signal, ecno_present));
assert(layout.offsets_data[5] == offsetof(mr_wcdma_signal, ecno));
}
{ mr_layout layout; assert(mr_layout_at(5, &layout) == MR_OK); assert(layout.name.len == strlen("mr_signal_strength")); assert(memcmp(layout.name.data,"mr_signal_strength",layout.name.len)==0); assert(layout.size == sizeof(mr_signal_strength)); assert(layout.alignment == _Alignof(mr_signal_strength)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_signal_strength, gsm_present));
assert(layout.offsets_data[1] == offsetof(mr_signal_strength, gsm));
assert(layout.offsets_data[2] == offsetof(mr_signal_strength, lte_present));
assert(layout.offsets_data[3] == offsetof(mr_signal_strength, lte));
assert(layout.offsets_data[4] == offsetof(mr_signal_strength, wcdma_present));
assert(layout.offsets_data[5] == offsetof(mr_signal_strength, wcdma));
}
{ mr_layout layout; assert(mr_layout_at(6, &layout) == MR_OK); assert(layout.name.len == strlen("mr_reg_state_result")); assert(memcmp(layout.name.data,"mr_reg_state_result",layout.name.len)==0); assert(layout.size == sizeof(mr_reg_state_result)); assert(layout.alignment == _Alignof(mr_reg_state_result)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_reg_state_result, reg_state));
assert(layout.offsets_data[1] == offsetof(mr_reg_state_result, rat));
assert(layout.offsets_data[2] == offsetof(mr_reg_state_result, cell_identity_present));
assert(layout.offsets_data[3] == offsetof(mr_reg_state_result, cell_identity));
assert(layout.offsets_data[4] == offsetof(mr_reg_state_result, reason_for_denial));
assert(layout.offsets_data[5] == offsetof(mr_reg_state_result, registered_plmn));
}
{ mr_layout layout; assert(mr_layout_at(7, &layout) == MR_OK); assert(layout.name.len == strlen("mr_operator_info")); assert(memcmp(layout.name.data,"mr_operator_info",layout.name.len)==0); assert(layout.size == sizeof(mr_operator_info)); assert(layout.alignment == _Alignof(mr_operator_info)); assert(layout.offsets_len == 3);
assert(layout.offsets_data[0] == offsetof(mr_operator_info, alpha_long));
assert(layout.offsets_data[1] == offsetof(mr_operator_info, alpha_short));
assert(layout.offsets_data[2] == offsetof(mr_operator_info, numeric));
}
{ mr_layout layout; assert(mr_layout_at(8, &layout) == MR_OK); assert(layout.name.len == strlen("mr_cell_info")); assert(memcmp(layout.name.data,"mr_cell_info",layout.name.len)==0); assert(layout.size == sizeof(mr_cell_info)); assert(layout.alignment == _Alignof(mr_cell_info)); assert(layout.offsets_len == 3);
assert(layout.offsets_data[0] == offsetof(mr_cell_info, registered));
assert(layout.offsets_data[1] == offsetof(mr_cell_info, identity));
assert(layout.offsets_data[2] == offsetof(mr_cell_info, signal));
}
{ mr_layout layout; assert(mr_layout_at(9, &layout) == MR_OK); assert(layout.name.len == strlen("mr_signal_criteria")); assert(memcmp(layout.name.data,"mr_signal_criteria",layout.name.len)==0); assert(layout.size == sizeof(mr_signal_criteria)); assert(layout.alignment == _Alignof(mr_signal_criteria)); assert(layout.offsets_len == 7);
assert(layout.offsets_data[0] == offsetof(mr_signal_criteria, access_network));
assert(layout.offsets_data[1] == offsetof(mr_signal_criteria, measurement));
assert(layout.offsets_data[2] == offsetof(mr_signal_criteria, enabled));
assert(layout.offsets_data[3] == offsetof(mr_signal_criteria, hysteresis_ms));
assert(layout.offsets_data[4] == offsetof(mr_signal_criteria, hysteresis_db));
assert(layout.offsets_data[5] == offsetof(mr_signal_criteria, thresholds_data));
assert(layout.offsets_data[6] == offsetof(mr_signal_criteria, thresholds_len));
}
{ mr_layout layout; assert(mr_layout_at(10, &layout) == MR_OK); assert(layout.name.len == strlen("mr_link_criteria")); assert(memcmp(layout.name.data,"mr_link_criteria",layout.name.len)==0); assert(layout.size == sizeof(mr_link_criteria)); assert(layout.alignment == _Alignof(mr_link_criteria)); assert(layout.offsets_len == 8);
assert(layout.offsets_data[0] == offsetof(mr_link_criteria, access_network));
assert(layout.offsets_data[1] == offsetof(mr_link_criteria, hysteresis_ms));
assert(layout.offsets_data[2] == offsetof(mr_link_criteria, hysteresis_dl_kbps));
assert(layout.offsets_data[3] == offsetof(mr_link_criteria, hysteresis_ul_kbps));
assert(layout.offsets_data[4] == offsetof(mr_link_criteria, thresholds_dl_kbps_data));
assert(layout.offsets_data[5] == offsetof(mr_link_criteria, thresholds_dl_kbps_len));
assert(layout.offsets_data[6] == offsetof(mr_link_criteria, thresholds_ul_kbps_data));
assert(layout.offsets_data[7] == offsetof(mr_link_criteria, thresholds_ul_kbps_len));
}
{ mr_layout layout; assert(mr_layout_at(11, &layout) == MR_OK); assert(layout.name.len == strlen("mr_imei_info")); assert(memcmp(layout.name.data,"mr_imei_info",layout.name.len)==0); assert(layout.size == sizeof(mr_imei_info)); assert(layout.alignment == _Alignof(mr_imei_info)); assert(layout.offsets_len == 3);
assert(layout.offsets_data[0] == offsetof(mr_imei_info, kind));
assert(layout.offsets_data[1] == offsetof(mr_imei_info, imei));
assert(layout.offsets_data[2] == offsetof(mr_imei_info, svn));
}
{ mr_layout layout; assert(mr_layout_at(12, &layout) == MR_OK); assert(layout.name.len == strlen("mr_device_identity")); assert(memcmp(layout.name.data,"mr_device_identity",layout.name.len)==0); assert(layout.size == sizeof(mr_device_identity)); assert(layout.alignment == _Alignof(mr_device_identity)); assert(layout.offsets_len == 4);
assert(layout.offsets_data[0] == offsetof(mr_device_identity, imei));
assert(layout.offsets_data[1] == offsetof(mr_device_identity, imeisv));
assert(layout.offsets_data[2] == offsetof(mr_device_identity, esn));
assert(layout.offsets_data[3] == offsetof(mr_device_identity, meid));
}
{ mr_layout layout; assert(mr_layout_at(13, &layout) == MR_OK); assert(layout.name.len == strlen("mr_activity_stats_tech_specific_info")); assert(memcmp(layout.name.data,"mr_activity_stats_tech_specific_info",layout.name.len)==0); assert(layout.size == sizeof(mr_activity_stats_tech_specific_info)); assert(layout.alignment == _Alignof(mr_activity_stats_tech_specific_info)); assert(layout.offsets_len == 4);
assert(layout.offsets_data[0] == offsetof(mr_activity_stats_tech_specific_info, rat));
assert(layout.offsets_data[1] == offsetof(mr_activity_stats_tech_specific_info, frequency_range));
assert(layout.offsets_data[2] == offsetof(mr_activity_stats_tech_specific_info, txm_mode_time_ms));
assert(layout.offsets_data[3] == offsetof(mr_activity_stats_tech_specific_info, rx_mode_time_ms));
}
{ mr_layout layout; assert(mr_layout_at(14, &layout) == MR_OK); assert(layout.name.len == strlen("mr_activity_stats_info")); assert(memcmp(layout.name.data,"mr_activity_stats_info",layout.name.len)==0); assert(layout.size == sizeof(mr_activity_stats_info)); assert(layout.alignment == _Alignof(mr_activity_stats_info)); assert(layout.offsets_len == 4);
assert(layout.offsets_data[0] == offsetof(mr_activity_stats_info, sleep_mode_time_ms));
assert(layout.offsets_data[1] == offsetof(mr_activity_stats_info, idle_mode_time_ms));
assert(layout.offsets_data[2] == offsetof(mr_activity_stats_info, tech_specific_info_data));
assert(layout.offsets_data[3] == offsetof(mr_activity_stats_info, tech_specific_info_len));
}
{ mr_layout layout; assert(mr_layout_at(15, &layout) == MR_OK); assert(layout.name.len == strlen("mr_radio_capability")); assert(memcmp(layout.name.data,"mr_radio_capability",layout.name.len)==0); assert(layout.size == sizeof(mr_radio_capability)); assert(layout.alignment == _Alignof(mr_radio_capability)); assert(layout.offsets_len == 5);
assert(layout.offsets_data[0] == offsetof(mr_radio_capability, session));
assert(layout.offsets_data[1] == offsetof(mr_radio_capability, phase));
assert(layout.offsets_data[2] == offsetof(mr_radio_capability, raf));
assert(layout.offsets_data[3] == offsetof(mr_radio_capability, logical_modem_uuid));
assert(layout.offsets_data[4] == offsetof(mr_radio_capability, status));
}
{ mr_layout layout; assert(mr_layout_at(16, &layout) == MR_OK); assert(layout.name.len == strlen("mr_slot_port_mapping")); assert(memcmp(layout.name.data,"mr_slot_port_mapping",layout.name.len)==0); assert(layout.size == sizeof(mr_slot_port_mapping)); assert(layout.alignment == _Alignof(mr_slot_port_mapping)); assert(layout.offsets_len == 2);
assert(layout.offsets_data[0] == offsetof(mr_slot_port_mapping, physical_slot_id));
assert(layout.offsets_data[1] == offsetof(mr_slot_port_mapping, port_id));
}
{ mr_layout layout; assert(mr_layout_at(17, &layout) == MR_OK); assert(layout.name.len == strlen("mr_app_status")); assert(memcmp(layout.name.data,"mr_app_status",layout.name.len)==0); assert(layout.size == sizeof(mr_app_status)); assert(layout.alignment == _Alignof(mr_app_status)); assert(layout.offsets_len == 8);
assert(layout.offsets_data[0] == offsetof(mr_app_status, app_type));
assert(layout.offsets_data[1] == offsetof(mr_app_status, app_state));
assert(layout.offsets_data[2] == offsetof(mr_app_status, perso_substate));
assert(layout.offsets_data[3] == offsetof(mr_app_status, aid_ptr));
assert(layout.offsets_data[4] == offsetof(mr_app_status, app_label_ptr));
assert(layout.offsets_data[5] == offsetof(mr_app_status, pin1_replaced));
assert(layout.offsets_data[6] == offsetof(mr_app_status, pin1));
assert(layout.offsets_data[7] == offsetof(mr_app_status, pin2));
}
{ mr_layout layout; assert(mr_layout_at(18, &layout) == MR_OK); assert(layout.name.len == strlen("mr_card_status")); assert(memcmp(layout.name.data,"mr_card_status",layout.name.len)==0); assert(layout.size == sizeof(mr_card_status)); assert(layout.alignment == _Alignof(mr_card_status)); assert(layout.offsets_len == 12);
assert(layout.offsets_data[0] == offsetof(mr_card_status, card_state));
assert(layout.offsets_data[1] == offsetof(mr_card_status, universal_pin_state));
assert(layout.offsets_data[2] == offsetof(mr_card_status, gsm_umts_subscription_app_index));
assert(layout.offsets_data[3] == offsetof(mr_card_status, cdma_subscription_app_index));
assert(layout.offsets_data[4] == offsetof(mr_card_status, ims_subscription_app_index));
assert(layout.offsets_data[5] == offsetof(mr_card_status, applications_data));
assert(layout.offsets_data[6] == offsetof(mr_card_status, applications_len));
assert(layout.offsets_data[7] == offsetof(mr_card_status, atr));
assert(layout.offsets_data[8] == offsetof(mr_card_status, iccid));
assert(layout.offsets_data[9] == offsetof(mr_card_status, eid));
assert(layout.offsets_data[10] == offsetof(mr_card_status, slot_map));
assert(layout.offsets_data[11] == offsetof(mr_card_status, supported_mep_mode));
}
{ mr_layout layout; assert(mr_layout_at(19, &layout) == MR_OK); assert(layout.name.len == strlen("mr_sim_port_info")); assert(memcmp(layout.name.data,"mr_sim_port_info",layout.name.len)==0); assert(layout.size == sizeof(mr_sim_port_info)); assert(layout.alignment == _Alignof(mr_sim_port_info)); assert(layout.offsets_len == 3);
assert(layout.offsets_data[0] == offsetof(mr_sim_port_info, icc_id));
assert(layout.offsets_data[1] == offsetof(mr_sim_port_info, logical_slot_id));
assert(layout.offsets_data[2] == offsetof(mr_sim_port_info, port_active));
}
{ mr_layout layout; assert(mr_layout_at(20, &layout) == MR_OK); assert(layout.name.len == strlen("mr_sim_slot_status")); assert(memcmp(layout.name.data,"mr_sim_slot_status",layout.name.len)==0); assert(layout.size == sizeof(mr_sim_slot_status)); assert(layout.alignment == _Alignof(mr_sim_slot_status)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_sim_slot_status, card_state));
assert(layout.offsets_data[1] == offsetof(mr_sim_slot_status, atr));
assert(layout.offsets_data[2] == offsetof(mr_sim_slot_status, eid));
assert(layout.offsets_data[3] == offsetof(mr_sim_slot_status, port_info_data));
assert(layout.offsets_data[4] == offsetof(mr_sim_slot_status, port_info_len));
assert(layout.offsets_data[5] == offsetof(mr_sim_slot_status, supported_mep_mode));
}
{ mr_layout layout; assert(mr_layout_at(21, &layout) == MR_OK); assert(layout.name.len == strlen("mr_phone_capability")); assert(memcmp(layout.name.data,"mr_phone_capability",layout.name.len)==0); assert(layout.size == sizeof(mr_phone_capability)); assert(layout.alignment == _Alignof(mr_phone_capability)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_phone_capability, max_active_data));
assert(layout.offsets_data[1] == offsetof(mr_phone_capability, max_active_internet_data));
assert(layout.offsets_data[2] == offsetof(mr_phone_capability, is_internet_lingering_supported));
assert(layout.offsets_data[3] == offsetof(mr_phone_capability, logical_modem_ids_data));
assert(layout.offsets_data[4] == offsetof(mr_phone_capability, logical_modem_ids_len));
assert(layout.offsets_data[5] == offsetof(mr_phone_capability, max_active_voice));
}
{ mr_layout layout; assert(mr_layout_at(22, &layout) == MR_OK); assert(layout.name.len == strlen("mr_gsm_broadcast_sms_config_info")); assert(memcmp(layout.name.data,"mr_gsm_broadcast_sms_config_info",layout.name.len)==0); assert(layout.size == sizeof(mr_gsm_broadcast_sms_config_info)); assert(layout.alignment == _Alignof(mr_gsm_broadcast_sms_config_info)); assert(layout.offsets_len == 5);
assert(layout.offsets_data[0] == offsetof(mr_gsm_broadcast_sms_config_info, from_service_id));
assert(layout.offsets_data[1] == offsetof(mr_gsm_broadcast_sms_config_info, to_service_id));
assert(layout.offsets_data[2] == offsetof(mr_gsm_broadcast_sms_config_info, from_code_scheme));
assert(layout.offsets_data[3] == offsetof(mr_gsm_broadcast_sms_config_info, to_code_scheme));
assert(layout.offsets_data[4] == offsetof(mr_gsm_broadcast_sms_config_info, selected));
}
{ mr_layout layout; assert(mr_layout_at(23, &layout) == MR_OK); assert(layout.name.len == strlen("mr_uus_info")); assert(memcmp(layout.name.data,"mr_uus_info",layout.name.len)==0); assert(layout.size == sizeof(mr_uus_info)); assert(layout.alignment == _Alignof(mr_uus_info)); assert(layout.offsets_len == 3);
assert(layout.offsets_data[0] == offsetof(mr_uus_info, uus_type));
assert(layout.offsets_data[1] == offsetof(mr_uus_info, uus_dcs));
assert(layout.offsets_data[2] == offsetof(mr_uus_info, uus_data));
}
{ mr_layout layout; assert(mr_layout_at(24, &layout) == MR_OK); assert(layout.name.len == strlen("mr_call")); assert(memcmp(layout.name.data,"mr_call",layout.name.len)==0); assert(layout.size == sizeof(mr_call)); assert(layout.alignment == _Alignof(mr_call)); assert(layout.offsets_len == 16);
assert(layout.offsets_data[0] == offsetof(mr_call, state));
assert(layout.offsets_data[1] == offsetof(mr_call, index));
assert(layout.offsets_data[2] == offsetof(mr_call, toa));
assert(layout.offsets_data[3] == offsetof(mr_call, is_mpty));
assert(layout.offsets_data[4] == offsetof(mr_call, is_mt));
assert(layout.offsets_data[5] == offsetof(mr_call, als));
assert(layout.offsets_data[6] == offsetof(mr_call, is_voice));
assert(layout.offsets_data[7] == offsetof(mr_call, is_voice_privacy));
assert(layout.offsets_data[8] == offsetof(mr_call, number));
assert(layout.offsets_data[9] == offsetof(mr_call, number_presentation));
assert(layout.offsets_data[10] == offsetof(mr_call, name));
assert(layout.offsets_data[11] == offsetof(mr_call, name_presentation));
assert(layout.offsets_data[12] == offsetof(mr_call, uus_info_data));
assert(layout.offsets_data[13] == offsetof(mr_call, uus_info_len));
assert(layout.offsets_data[14] == offsetof(mr_call, audio_quality));
assert(layout.offsets_data[15] == offsetof(mr_call, forwarded_number));
}
{ mr_layout layout; assert(mr_layout_at(25, &layout) == MR_OK); assert(layout.name.len == strlen("mr_request")); assert(memcmp(layout.name.data,"mr_request",layout.name.len)==0); assert(layout.size == sizeof(mr_request)); assert(layout.alignment == _Alignof(mr_request)); assert(layout.offsets_len == 15);
assert(layout.offsets_data[0] == offsetof(mr_request, kind));
assert(layout.offsets_data[1] == offsetof(mr_request, boolean));
assert(layout.offsets_data[2] == offsetof(mr_request, mask));
assert(layout.offsets_data[3] == offsetof(mr_request, integer));
assert(layout.offsets_data[4] == offsetof(mr_request, small_integer));
assert(layout.offsets_data[5] == offsetof(mr_request, profile_count));
assert(layout.offsets_data[6] == offsetof(mr_request, power_on));
assert(layout.offsets_data[7] == offsetof(mr_request, for_emergency_call));
assert(layout.offsets_data[8] == offsetof(mr_request, preferred_for_emergency_call));
assert(layout.offsets_data[9] == offsetof(mr_request, device_state_type));
assert(layout.offsets_data[10] == offsetof(mr_request, signal_criteria_data));
assert(layout.offsets_data[11] == offsetof(mr_request, signal_criteria_len));
assert(layout.offsets_data[12] == offsetof(mr_request, link_criteria));
assert(layout.offsets_data[13] == offsetof(mr_request, broadcast_config_data));
assert(layout.offsets_data[14] == offsetof(mr_request, broadcast_config_len));
}
{ mr_layout layout; assert(mr_layout_at(26, &layout) == MR_OK); assert(layout.name.len == strlen("mr_value")); assert(memcmp(layout.name.data,"mr_value",layout.name.len)==0); assert(layout.size == sizeof(mr_value)); assert(layout.alignment == _Alignof(mr_value)); assert(layout.offsets_len == 20);
assert(layout.offsets_data[0] == offsetof(mr_value, kind));
assert(layout.offsets_data[1] == offsetof(mr_value, boolean));
assert(layout.offsets_data[2] == offsetof(mr_value, integer));
assert(layout.offsets_data[3] == offsetof(mr_value, text));
assert(layout.offsets_data[4] == offsetof(mr_value, registration));
assert(layout.offsets_data[5] == offsetof(mr_value, operator_info));
assert(layout.offsets_data[6] == offsetof(mr_value, signal));
assert(layout.offsets_data[7] == offsetof(mr_value, imei));
assert(layout.offsets_data[8] == offsetof(mr_value, identity));
assert(layout.offsets_data[9] == offsetof(mr_value, activity));
assert(layout.offsets_data[10] == offsetof(mr_value, capability));
assert(layout.offsets_data[11] == offsetof(mr_value, card));
assert(layout.offsets_data[12] == offsetof(mr_value, phone));
assert(layout.offsets_data[13] == offsetof(mr_value, selection));
assert(layout.offsets_data[14] == offsetof(mr_value, cells_data));
assert(layout.offsets_data[15] == offsetof(mr_value, cells_len));
assert(layout.offsets_data[16] == offsetof(mr_value, slots_data));
assert(layout.offsets_data[17] == offsetof(mr_value, slots_len));
assert(layout.offsets_data[18] == offsetof(mr_value, calls_data));
assert(layout.offsets_data[19] == offsetof(mr_value, calls_len));
}
{ mr_layout layout; assert(mr_layout_at(27, &layout) == MR_OK); assert(layout.name.len == strlen("mr_indication")); assert(memcmp(layout.name.data,"mr_indication",layout.name.len)==0); assert(layout.size == sizeof(mr_indication)); assert(layout.alignment == _Alignof(mr_indication)); assert(layout.offsets_len == 4);
assert(layout.offsets_data[0] == offsetof(mr_indication, kind));
assert(layout.offsets_data[1] == offsetof(mr_indication, signal));
assert(layout.offsets_data[2] == offsetof(mr_indication, radio_state));
assert(layout.offsets_data[3] == offsetof(mr_indication, generation));
}
{ mr_layout layout; assert(mr_layout_at(28, &layout) == MR_OK); assert(layout.name.len == strlen("mr_output")); assert(memcmp(layout.name.data,"mr_output",layout.name.len)==0); assert(layout.size == sizeof(mr_output)); assert(layout.alignment == _Alignof(mr_output)); assert(layout.offsets_len == 6);
assert(layout.offsets_data[0] == offsetof(mr_output, kind));
assert(layout.offsets_data[1] == offsetof(mr_output, invocation_id));
assert(layout.offsets_data[2] == offsetof(mr_output, error));
assert(layout.offsets_data[3] == offsetof(mr_output, value_present));
assert(layout.offsets_data[4] == offsetof(mr_output, value));
assert(layout.offsets_data[5] == offsetof(mr_output, indication));
}
{ mr_layout layout; assert(mr_layout_at(29, &layout) == MR_OK); assert(layout.name.len == strlen("mr_layout")); assert(memcmp(layout.name.data,"mr_layout",layout.name.len)==0); assert(layout.size == sizeof(mr_layout)); assert(layout.alignment == _Alignof(mr_layout)); assert(layout.offsets_len == 5);
assert(layout.offsets_data[0] == offsetof(mr_layout, name));
assert(layout.offsets_data[1] == offsetof(mr_layout, size));
assert(layout.offsets_data[2] == offsetof(mr_layout, alignment));
assert(layout.offsets_data[3] == offsetof(mr_layout, offsets_data));
assert(layout.offsets_data[4] == offsetof(mr_layout, offsets_len));
}
mr_batch batch = 0;
mr_runtime runtime = 0;
uint32_t count = 0;
const mr_output *output = NULL;
assert(mr_create(NULL) == MR_INVALID_ARGUMENT);
assert(mr_next_batch(0,0,&batch) == MR_INVALID_HANDLE);
assert(mr_batch_count(0,&count) == MR_INVALID_HANDLE);
assert(mr_batch_at(0,0,&output) == MR_INVALID_HANDLE);
assert(mr_stop(0) == MR_INVALID_HANDLE);
assert(mr_batch_free(&batch) == MR_INVALID_HANDLE);
assert(mr_destroy(&runtime) == MR_INVALID_HANDLE);
assert(mr_submit(0,1,NULL) == MR_INVALID_ARGUMENT);
assert(mr_layout_at(UINT32_MAX,NULL) == MR_INVALID_ARGUMENT);
printf("C layouts: %u structs verified\n",mr_layout_count());
return 0;
}
