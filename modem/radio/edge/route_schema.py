# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

ROUTES = []


def add(service, method, kind, shape, field=None, encoder=None, validation=None, aliases=()):
    ROUTES.append(dict(service=service, method=method, kind=kind, shape=shape, field=field, encoder=encoder, validation=validation, aliases=list(aliases)))


for method, suffix, shape in [
    ('getVoiceRegistrationState','GET_VOICE_REGISTRATION_STATE','registration'),
    ('getDataRegistrationState','GET_DATA_REGISTRATION_STATE','registration'),
    ('getOperator','GET_OPERATOR','operator'),
    ('getNetworkSelectionMode','GET_NETWORK_SELECTION_MODE','selection'),
    ('getSignalStrength','GET_SIGNAL_STRENGTH','signal'),
    ('getCellInfoList','GET_CELL_INFO_LIST','cells'),
    ('getImsRegistrationState','GET_IMS_REGISTRATION_STATE','unrepresentable'),
]:
    add('network',method,'MR_REQUEST_NETWORK_'+suffix,shape)
add('network','setIndicationFilter','MR_REQUEST_NETWORK_SET_INDICATION_FILTER','network_void','mask')
add('network','setSignalStrengthReportingCriteria','MR_REQUEST_NETWORK_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA','network_void',encoder='encodeSignalCriteria')
add('network','setLinkCapacityReportingCriteria','MR_REQUEST_NETWORK_SET_LINK_CAPACITY_REPORTING_CRITERIA','network_void',encoder='encodeLinkCriteria')
SHAPES = {
    'network_void': ('MR_VALUE_NETWORK_VOID', []),
    'registration': ('MR_VALUE_NETWORK_REGISTRATION', [('registration','value->registration')]),
    'operator': ('MR_VALUE_NETWORK_OPERATOR', [('text','value->operator_info.alpha_long'),('text','value->operator_info.alpha_short'),('text','value->operator_info.numeric')]),
    'selection': ('MR_VALUE_NETWORK_SELECTION', [('selection','value->selection')]),
    'signal': ('MR_VALUE_NETWORK_SIGNAL', [('signal','value->signal')]),
    'cells': ('MR_VALUE_NETWORK_CELLS', [('cells','*value')]),
}

add('modem', 'getBasebandVersion', 'MR_REQUEST_MODEM_GET_BASEBAND_VERSION', 'baseband', field=None, encoder=None, validation=None, aliases=())
add('modem', 'getImei', 'MR_REQUEST_MODEM_GET_IMEI', 'imei', field=None, encoder=None, validation=None, aliases=())
add('modem', 'getDeviceIdentity', 'MR_REQUEST_MODEM_GET_DEVICE_IDENTITY', 'identity', field=None, encoder=None, validation=None, aliases=())
add('modem', 'getModemActivityInfo', 'MR_REQUEST_MODEM_GET_MODEM_ACTIVITY_INFO', 'activity', field=None, encoder=None, validation=None, aliases=())
add('modem', 'getModemStackStatus', 'MR_REQUEST_MODEM_GET_MODEM_STACK_STATUS', 'unrepresentable', field=None, encoder=None, validation=None, aliases=())
add('modem', 'getRadioCapability', 'MR_REQUEST_MODEM_GET_RADIO_CAPABILITY', 'capability', field=None, encoder=None, validation=None, aliases=())
add('modem', 'setRadioPower', 'MR_REQUEST_MODEM_SET_RADIO_POWER', 'modem_void', field=None, encoder='encodePower', validation=None, aliases=())
add('modem', 'enableModem', 'MR_REQUEST_MODEM_ENABLE_MODEM', 'modem_void', field='boolean', encoder=None, validation=None, aliases=())
add('modem', 'sendDeviceState', 'MR_REQUEST_MODEM_SEND_DEVICE_STATE', 'modem_void', field=None, encoder='encodeDeviceState', validation=None, aliases=())
add('sim', 'getIccCardStatus', 'MR_REQUEST_MODEM_GET_ICC_CARD_STATUS', 'card', field=None, encoder=None, validation=None, aliases=())
add('sim', 'areUiccApplicationsEnabled', 'MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED', 'unrepresentable', field=None, encoder=None, validation=None, aliases=('MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT',))
add('sim', 'enableUiccApplications', 'MR_REQUEST_MODEM_ENABLE_UICC_APPLICATIONS', 'modem_void', field='boolean', encoder=None, validation=None, aliases=())
add('sim', 'setCarrierInfoForImsiEncryption', 'MR_REQUEST_MODEM_SET_CARRIER_INFO_FOR_IMSI_ENCRYPTION', 'modem_void', field=None, encoder='encodeImsiEncryption', validation=None, aliases=())
add('config', 'getHalDeviceCapabilities', 'MR_REQUEST_MODEM_GET_HAL_DEVICE_CAPABILITIES', 'reduced', field=None, encoder=None, validation=None, aliases=())
add('config', 'getSimSlotsStatus', 'MR_REQUEST_MODEM_GET_SIM_SLOTS_STATUS', 'slots', field=None, encoder=None, validation=None, aliases=())
add('config', 'getPhoneCapability', 'MR_REQUEST_MODEM_GET_PHONE_CAPABILITY', 'phone', field=None, encoder=None, validation=None, aliases=())
add('config', 'getNumOfLiveModems', 'MR_REQUEST_MODEM_GET_NUM_OF_LIVE_MODEMS', 'unrepresentable', field=None, encoder=None, validation=None, aliases=())
add('config', 'setNumOfLiveModems', 'MR_REQUEST_MODEM_SET_NUM_OF_LIVE_MODEMS', 'modem_void', field='small_integer', encoder=None, validation='positive', aliases=())
add('config', 'setPreferredDataModem', 'MR_REQUEST_MODEM_SET_PREFERRED_DATA_MODEM', 'modem_void', field='small_integer', encoder=None, validation='nonnegative', aliases=())
add('voice', 'getCurrentCalls', 'MR_REQUEST_MODEM_GET_CURRENT_CALLS', 'calls', field=None, encoder=None, validation=None, aliases=())
add('voice', 'setTtyMode', 'MR_REQUEST_MODEM_SET_TTY_MODE', 'modem_void', field='integer', encoder=None, validation='tty', aliases=())
add('messaging', 'setGsmBroadcastConfig', 'MR_REQUEST_MODEM_SET_GSM_BROADCAST_CONFIG', 'modem_void', field=None, encoder='encodeBroadcast', validation=None, aliases=())
add('data', 'setDataProfile', 'MR_REQUEST_MODEM_SET_DATA_PROFILE', 'modem_void', field=None, encoder='encodeDataProfiles', validation=None, aliases=())
add('network', 'setNullCipherAndIntegrityEnabled', 'MR_REQUEST_MODEM_SET_NULL_CIPHER_AND_INTEGRITY_ENABLED', 'modem_void', field='boolean', encoder=None, validation=None, aliases=())
add('network', 'setSecurityAlgorithmsUpdatedEnabled', 'MR_REQUEST_MODEM_SET_SECURITY_ALGORITHMS_UPDATED_ENABLED', 'modem_void', field='boolean', encoder=None, validation=None, aliases=())
add('network', 'setCellularIdentifierTransparencyEnabled', 'MR_REQUEST_MODEM_SET_CELLULAR_IDENTIFIER_DISCLOSED_ENABLED', 'modem_void', field='boolean', encoder=None, validation=None, aliases=())
add('network', 'isSatelliteEnabledForCarrier', 'MR_REQUEST_MODEM_IS_SATELLITE_ENABLED_FOR_CARRIER', 'unrepresentable', field=None, encoder=None, validation=None, aliases=())
add('network', 'getBarringInfo', 'MR_REQUEST_MODEM_GET_BARRING_INFO', 'unrepresentable', field=None, encoder=None, validation=None, aliases=())
add('network', 'getVoiceRadioTechnology', 'MR_REQUEST_MODEM_GET_VOICE_RADIO_TECHNOLOGY', 'voice_technology', field=None, encoder=None, validation=None, aliases=())

SHAPES.update({
    'modem_void': ('MR_VALUE_MODEM_VOID', []),
    'baseband': ('MR_VALUE_MODEM_BASEBAND', [('text','value->text')]),
    'imei': ('MR_VALUE_MODEM_IMEI', [('imei','value->imei')]),
    'identity': ('MR_VALUE_MODEM_IDENTITY', [('text','value->identity.imei'),('text','value->identity.imeisv'),('text','value->identity.esn'),('text','value->identity.meid')]),
    'activity': ('MR_VALUE_MODEM_ACTIVITY', [('activity','value->activity')]),
    'capability': ('MR_VALUE_MODEM_CAPABILITY', [('capability','value->capability')]),
    'card': ('MR_VALUE_MODEM_CARD', [('card','value->card')]),
    'slots': ('MR_VALUE_MODEM_SLOTS', [('slots','*value')]),
    'phone': ('MR_VALUE_MODEM_PHONE', [('phone','value->phone')]),
    'reduced': ('MR_VALUE_MODEM_REDUCED_FEATURE_SET1', [('boolean','value->boolean')]),
    'calls': ('MR_VALUE_MODEM_CURRENT_CALLS', [('calls','*value')]),
    'voice_technology': ('MR_VALUE_MODEM_VOICE_RADIO_TECHNOLOGY', [('technology','value->integer')]),
})
DEFERRED = []
