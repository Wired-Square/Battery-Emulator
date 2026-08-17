import { getJson, postJson } from '/app.js';

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

const CATEGORIES = [
  ['network', 'Network'],
  ['webauth', 'Web access'],
  ['battery', 'Battery'],
  ['inverter', 'Inverter'],
  ['optional', 'Optional'],
  ['hardware', 'Hardware'],
  ['connectivity', 'Connectivity'],
  ['debug', 'Debug'],
];

// Ordered by id, matching the server's emission (e.g. pack is 50/74/62/100, not by capacity).
const CLIENT_OPTIONS = {
  country: [
    { v: 16725, n: 'AU (Australia)' },
    { v: 17217, n: 'CA (Canada)' },
    { v: 17477, n: 'DE (Germany)' },
    { v: 17483, n: 'DK (Denmark)' },
    { v: 18242, n: 'GB (UK & N Ireland)' },
    { v: 21843, n: 'US (USA)' },
  ],
  mapregion: [
    { v: 0, n: 'US (USA)' },
    { v: 1, n: 'EU (Europe)' },
    { v: 2, n: 'NONE' },
    { v: 3, n: 'CN (China)' },
    { v: 4, n: 'AU (Australia)' },
    { v: 5, n: 'JP (Japan)' },
    { v: 6, n: 'TW (Taiwan)' },
    { v: 7, n: 'KR (Korea)' },
    { v: 8, n: 'ME (Middle East)' },
  ],
  chassis: [
    { v: 0, n: 'Model S' },
    { v: 1, n: 'Model X' },
    { v: 2, n: 'Model 3' },
    { v: 3, n: 'Model Y' },
  ],
  pack: [
    { v: 0, n: '50 kWh' },
    { v: 1, n: '74 kWh' },
    { v: 2, n: '62 kWh' },
    { v: 3, n: '100 kWh' },
  ],
  sungrow: [
    { v: 0, n: 'SBR064 (6.4 kWh, 2 modules)' },
    { v: 1, n: 'SBR096 (9.6 kWh, 3 modules)' },
    { v: 2, n: 'SBR128 (12.8 kWh, 4 modules)' },
    { v: 3, n: 'SBR160 (16.0 kWh, 5 modules)' },
    { v: 4, n: 'SBR192 (19.2 kWh, 6 modules)' },
    { v: 5, n: 'SBR224 (22.4 kWh, 7 modules)' },
    { v: 6, n: 'SBR256 (25.6 kWh, 8 modules)' },
  ],
  pylonbrand: [
    { v: 0, n: 'PYLONTECH' },
    { v: 1, n: 'PYLON' },
    { v: 2, n: 'DEYE' },
  ],
  contactor: [
    { v: 0, n: 'No Workaround' },
    { v: 1, n: 'Keep contactors always closed' },
    { v: 2, n: 'Lock contactors closed after first close request' },
  ],
};

const LABELS = {
  WEBAUTH: 'Enable password protection',
  HTTPUSER: 'Username',
  HTTPPASS: 'Web interface password',
  HTTPPASSCONFIRM: 'Repeat web interface password',
  SSID: 'SSID',
  PASSWORD: 'Password',
  battery: 'Battery',
  BATTCHEM: 'Battery chemistry',
  BATTCOMM: 'Battery interface',
  BATTPVMAX: 'Battery max design voltage (V)',
  BATTPVMIN: 'Battery min design voltage (V)',
  BATTCVMAX: 'Cell max design voltage (mV)',
  BATTCVMIN: 'Cell min design voltage (mV)',
  PYLONBAUD: 'Pylon CAN baudrate (kbps)',
  battery2: 'Battery 2',
  BATT2COMM: 'Battery 2 interface',
  battery3: 'Battery 3',
  BATT3COMM: 'Battery 3 interface',
  INTERLOCKREQ: 'Interlock required',
  SOCESTIMATED: 'Use estimated SOC',
  DALYPWRPCT: 'Power limit per percent SOC above 80 / below 20 (W/pct)',
  DALYPWRDV: 'Max power per dV distance from minimum voltage (W/dV)',
  DALYDVSTART: 'Voltage difference for start of voltage based discharge limit (dV)',
  DALYPWRDEG: 'Power change per °C above/below 0°C (W/°C)',
  DALYPWR0C: 'Power at 0°C (W)',
  DIGITALHVIL: 'Digital HVIL (2024+)',
  GTWRHD: 'Right hand drive',
  GTWCOUNTRY: 'Country code',
  GTWMAPREG: 'Map region',
  GTWCHASSIS: 'Chassis type',
  GTWPACK: 'Pack type',
  CHGPOWER: 'Manual charging power, watt',
  DCHGPOWER: 'Manual discharge power, watt',
  RAMPDOWNSOC: 'Rampdown SOC, pptt',
  SOFAR_ID: 'Sofar Battery ID (0-15)',
  inverter: 'Inverter protocol',
  INVCOMM: 'Inverter interface',
  LOWPASSFILTER: 'Ramp up charge limits gradually',
  CHGESTIMATED: 'Use estimated charge/discharge limits',
  CHGTAPERSOC: 'Charge power tapering based on SOC',
  CHGTAPERSTART: 'Start tapering at SOC, percent',
  CHGTAPERFLOOR: 'Float charge power, W',
  SLOWCANINV: 'Allow longer CAN timeout',
  PYLONSEND: 'Pylon, send group (0-1)',
  PYLONOFFSET: 'Pylon, 30k offset',
  PYLONORDER: 'Pylon, invert byteorder',
  PYLONBRAND: 'Pylon, manufacturer name',
  DEYEBYD: 'Deye avoid over/undercharge fix',
  PRIMOGEN24: 'Fronius Primo, 450V maxvoltage cap',
  INVCELLS: 'Reported cell count (0 for default)',
  INVMODULES: 'Reported module count (0 for default)',
  INVCELLSPER: 'Reported cells per module (0 for default)',
  INVVLEVEL: 'Reported voltage level (0 for default)',
  INVCAPACITY: 'Reported Ah capacity (0 for default)',
  INVBTYPE: 'Reported battery type (in decimal)',
  INVSUNTYPE: 'Battery model',
  INVICNT: 'Inverter Contactor Workaround',
  FOXESSTYPE: 'FoxESS battery type (0 for default)',
  FOXESSSUBTYPE: 'FoxESS battery subtype (0 for default)',
  FOXESSMODULES: 'FoxESS module count (0 for default)',
  charger: 'Charger',
  CHGCOMM: 'Charger interface',
  shunttype: 'Shunt',
  SHUNTCOMM: 'Shunt interface',
  CTOFFSET: 'CT Clamp offset (mV)',
  CTVNOM: 'CT Clamp nominal voltage (dV)',
  CTANOM: 'CT Clamp nominal current (A)',
  CTATTEN: 'ESP32 pin attenuation',
  CTINVERT: 'Invert CT current',
  CANFDASCAN: 'Use CanFD as classic CAN',
  CANFD2ASCAN: 'Use CanFD2 as classic CAN',
  EQSTOP: 'Equipment stop button',
  CNTCTRL: 'Contactor control via GPIO',
  CNTCTRLDBL: 'Double-Battery Contactor control via GPIO',
  CNTCTRLTRI: 'Triple-Battery Contactor control via GPIO',
  PRECHGMS: 'Precharge time ms',
  NCCONTACTOR: 'Use Normally Closed logic',
  PWMCNTCTRL: 'PWM contactor control',
  PWMFREQ: 'PWM Frequency Hz',
  PWMHOLD: 'PWM Hold 1-1023',
  PERBMSRESET: 'Periodic BMS reset every 24h',
  EXTPRECHARGE: 'External precharge via HIA4V1',
  MAXPRETIME: 'Precharge, maximum ms before fault',
  MAXPREFREQ: 'Precharge, maximum PWM frequency',
  NOINVDISC: 'Normally Open (NO) inverter disconnect contactor',
  LEDMODE: 'Status LED pattern',
  WIFIAPENABLED: 'Broadcast Wifi access point',
  APPASSWORD: 'Access point password',
  WIFICHANNEL: 'Wifi channel 0-14',
  HOSTNAME: 'Custom Wifi hostname',
  STATICIP: 'Use static IP address',
  LOCALIP: 'Local IP address',
  GATEWAY: 'Gateway',
  SUBNET: 'Subnet mask',
  DNS: 'DNS server',
  ESPNOWENABLED: 'Enable ESPNow',
  ESPNOWMACS: 'ESPNow receiver MACs',
  MQTTENABLED: 'Enable MQTT',
  MQTTSERVER: 'MQTT server',
  MQTTPORT: 'MQTT port',
  MQTTUSER: 'MQTT user',
  MQTTPASSWORD: 'MQTT password',
  MQTTTIMEOUT: 'MQTT timeout ms',
  MQTTPUBLISHMS: 'MQTT publish interval (seconds)',
  MQTTCELLV: 'Send all cellvoltages via MQTT',
  MQTTHEAP: 'Publish heap metric diagnostics',
  REMBMSRESET: 'Remote BMS reset via MQTT allowed',
  HADISC: 'Publish Home Assistant autodiscovery at next boot',
  HADISCFWU: 'Publish Home Assistant autodiscovery at firmware updates',
  HADISCTOPIC: 'Home Assistant autodiscovery topic',
  PERFPROFILE: 'Enable performance profiling on main page',
  CANLOGUSB: 'Enable CAN message logging via USB serial',
  USBENABLED: 'Enable general logging via USB serial',
  WEBENABLED: 'Enable general logging via Webserver',
  CANLOGSD: 'Enable CAN message logging via SD card',
  SDLOGENABLED: 'Enable general logging via SD card',
  SYSLOGEN: 'General logging to syslog server',
  SYSLOGIP: 'Syslog server IP',
  SYSLOGPORT: 'Syslog UDP port',
  SYSLOGFAC: 'Syslog facility',
};

// Client-only text-field regexes (numeric bounds ride the schema). An empty value passes
// natively, so optional blank fields (unset IPs, hostname, anonymous MQTT) still save.
const IPV4_PATTERN = '((25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(25[0-5]|2[0-4]\\d|1?\\d?\\d)';
const HELP = {
  HADISC:
    'Publish the discovery configs once after the next restart. Clears itself once they have been published.',
  HADISCFWU:
    'Publish the discovery configs once after every firmware update. They carry the software version and can gain or change entities between releases.',
  HADISCTOPIC: "MQTT auto discovery base topic (letters, numbers, '_', '-')",
  ESPNOWENABLED: 'Send battery telemetry to nearby devices over ESP-NOW',
  ESPNOWMACS:
    'Comma separated list of receiver MAC addresses, e.g. AA:BB:CC:DD:EE:FF, 11:22:33:44:55:66 (max 8). Leave empty to broadcast to every device. Takes effect after a restart.',
};

const PATTERNS = {
  SYSLOGIP: IPV4_PATTERN,
  LOCALIP: IPV4_PATTERN,
  GATEWAY: IPV4_PATTERN,
  SUBNET: IPV4_PATTERN,
  DNS: IPV4_PATTERN,
  HOSTNAME: '[A-Za-z0-9_\\-]+',
  MQTTSERVER: '[A-Za-z0-9.\\-]+',
  HADISCTOPIC: '[A-Za-z0-9_\\-]+',
  ESPNOWMACS:
    '\\s*[0-9A-Fa-f]{2}([:\\-]?[0-9A-Fa-f]{2}){5}(\\s*[,;]\\s*[0-9A-Fa-f]{2}([:\\-]?[0-9A-Fa-f]{2}){5})*\\s*',
  CTOFFSET: '-?[0-9]+(\\.[0-9]+)?',
  SSID: '[ -~]{1,63}',
  HTTPUSER: '[ -~]{1,32}',
  MQTTUSER: '[ -~]+',
  MQTTPASSWORD: '[ -~]+',
  // WPA2 length floor: a sub-8-char password leaves the network down.
  APPASSWORD: '[ -~]{8,63}',
  PASSWORD: '[ -~]{8,63}',
};

const PASSWORD_KEYS = new Set(['PASSWORD', 'HTTPPASS', 'HTTPPASSCONFIRM', 'APPASSWORD', 'MQTTPASSWORD']);
// Stored secrets that can be explicitly cleared (sent as JSON null). HTTPPASSCONFIRM is not stored.
const CLEARABLE_PASSWORD_KEYS = new Set(['PASSWORD', 'HTTPPASS', 'APPASSWORD', 'MQTTPASSWORD']);

// BatteryType / InverterProtocolType enum values, mirrored from the firmware enums.
const BAT = {
  CBMS: [6, 11, 22, 23, 24, 31, 41, 48, 49, 51],
  NISSAN: [21],
  DALY: [23],
  TESLA: [32, 33],
  ESTIMATED: [3, 4, 6, 8, 14, 16, 24, 26, 32, 33, 40, 41, 44, 50, 51],
  CHGEST: [8, 26, 44],
  SOCEST: [16, 26, 41, 42],
  PYLON: [22],
  BMWPHEV: [43],
};
const INV = {
  SOFAR: [17],
  BYD: [2],
  BYDMODBUS: [3],
  PYLON: [10],
  PYLONISH: [4, 10, 19],
  SOLAX: [18],
  SUNGROW: [21],
  KOSTAL: [9],
  FOXESS: [5],
};
const SHUNT_CTCLAMP = 3;
const TESLA_CHASSIS_MODEL_3 = 2;

const inList = (list, value) => list.includes(Number(value));

// Value-driven show/hide; board-gated absence is already handled by the schema.
const VISIBILITY = {
  BATTCOMM: (s) => Number(s.battery) !== 0,
  BATTCHEM: (s) => Number(s.battery) !== 0,
  PYLONBAUD: (s) => inList(BAT.PYLON, s.battery),
  BATTPVMAX: (s) => inList(BAT.CBMS, s.battery),
  BATTPVMIN: (s) => inList(BAT.CBMS, s.battery),
  BATTCVMAX: (s) => inList(BAT.CBMS, s.battery),
  BATTCVMIN: (s) => inList(BAT.CBMS, s.battery),
  INTERLOCKREQ: (s) => inList(BAT.NISSAN, s.battery),
  DALYPWRPCT: (s) => inList(BAT.DALY, s.battery),
  DALYDVSTART: (s) => inList(BAT.DALY, s.battery),
  DALYPWRDV: (s) => inList(BAT.DALY, s.battery),
  DALYPWRDEG: (s) => inList(BAT.DALY, s.battery),
  DALYPWR0C: (s) => inList(BAT.DALY, s.battery),
  DIGITALHVIL: (s) => inList(BAT.TESLA, s.battery),
  GTWRHD: (s) => inList(BAT.TESLA, s.battery),
  GTWCOUNTRY: (s) => inList(BAT.TESLA, s.battery),
  GTWMAPREG: (s) => inList(BAT.TESLA, s.battery),
  GTWCHASSIS: (s) => inList(BAT.TESLA, s.battery),
  GTWPACK: (s) => inList(BAT.TESLA, s.battery),
  CHGPOWER: (s) => inList(BAT.ESTIMATED, s.battery),
  DCHGPOWER: (s) => inList(BAT.ESTIMATED, s.battery),
  RAMPDOWNSOC: (s) => inList(BAT.ESTIMATED, s.battery),
  SOCESTIMATED: (s) => inList(BAT.SOCEST, s.battery),
  CHGESTIMATED: (s) => inList(BAT.CHGEST, s.battery),
  BATT2COMM: (s) => Number(s.battery2) !== 0,
  battery3: (s) => Number(s.battery2) !== 0 || Number(s.battery3) !== 0,
  BATT3COMM: (s) => Number(s.battery3) !== 0,
  SOFAR_ID: (s) => inList(INV.SOFAR, s.inverter),
  INVCOMM: (s) => Number(s.inverter) !== 0,
  LOWPASSFILTER: (s) => Number(s.inverter) !== 0,
  CHGTAPERSOC: (s) => Number(s.inverter) !== 0,
  CHGTAPERSTART: (s) => Number(s.inverter) !== 0 && s.CHGTAPERSOC === true,
  CHGTAPERFLOOR: (s) => Number(s.inverter) !== 0 && s.CHGTAPERSOC === true,
  SLOWCANINV: (s) => Number(s.inverter) !== 0,
  PYLONSEND: (s) => inList(INV.PYLON, s.inverter),
  PYLONOFFSET: (s) => inList(INV.PYLON, s.inverter),
  PYLONORDER: (s) => inList(INV.PYLON, s.inverter),
  PYLONBRAND: (s) => inList(INV.PYLON, s.inverter),
  DEYEBYD: (s) => inList(INV.BYD, s.inverter),
  PRIMOGEN24: (s) => inList(INV.BYDMODBUS, s.inverter),
  INVCELLS: (s) => inList(INV.PYLONISH, s.inverter),
  INVCELLSPER: (s) => inList(INV.PYLONISH, s.inverter),
  INVVLEVEL: (s) => inList(INV.PYLONISH, s.inverter),
  INVCAPACITY: (s) => inList(INV.PYLONISH, s.inverter),
  INVMODULES: (s) => inList(INV.PYLONISH, s.inverter) || inList(INV.SOLAX, s.inverter),
  INVBTYPE: (s) => inList(INV.SOLAX, s.inverter),
  INVSUNTYPE: (s) => inList(INV.SUNGROW, s.inverter),
  INVICNT: (s) => inList(INV.KOSTAL, s.inverter) || inList(INV.SOLAX, s.inverter),
  FOXESSTYPE: (s) => inList(INV.FOXESS, s.inverter),
  FOXESSSUBTYPE: (s) => inList(INV.FOXESS, s.inverter),
  FOXESSMODULES: (s) => inList(INV.FOXESS, s.inverter),
  CHGCOMM: (s) => Number(s.charger) !== 0,
  SHUNTCOMM: (s) => Number(s.shunttype) !== 0 && Number(s.shunttype) !== SHUNT_CTCLAMP,
  CTOFFSET: (s) => Number(s.shunttype) === SHUNT_CTCLAMP,
  CTVNOM: (s) => Number(s.shunttype) === SHUNT_CTCLAMP,
  CTANOM: (s) => Number(s.shunttype) === SHUNT_CTCLAMP,
  CTATTEN: (s) => Number(s.shunttype) === SHUNT_CTCLAMP,
  CTINVERT: (s) => Number(s.shunttype) === SHUNT_CTCLAMP,
  CNTCTRLDBL: (s) => Number(s.battery2) !== 0,
  CNTCTRLTRI: (s) => Number(s.battery3) !== 0,
  PRECHGMS: (s) => s.CNTCTRL === true,
  NCCONTACTOR: (s) => s.CNTCTRL === true,
  PWMCNTCTRL: (s) => s.CNTCTRL === true,
  PWMFREQ: (s) => s.CNTCTRL === true && s.PWMCNTCTRL === true,
  PWMHOLD: (s) => s.CNTCTRL === true && s.PWMCNTCTRL === true,
  MAXPRETIME: (s) => s.EXTPRECHARGE === true,
  MAXPREFREQ: (s) => s.EXTPRECHARGE === true,
  NOINVDISC: (s) => s.EXTPRECHARGE === true,
  LOCALIP: (s) => s.STATICIP === true,
  GATEWAY: (s) => s.STATICIP === true,
  SUBNET: (s) => s.STATICIP === true,
  DNS: (s) => s.STATICIP === true,
  ESPNOWMACS: (s) => s.ESPNOWENABLED === true,
  MQTTSERVER: (s) => s.MQTTENABLED === true,
  MQTTPORT: (s) => s.MQTTENABLED === true,
  MQTTUSER: (s) => s.MQTTENABLED === true,
  MQTTPASSWORD: (s) => s.MQTTENABLED === true,
  MQTTTIMEOUT: (s) => s.MQTTENABLED === true,
  MQTTPUBLISHMS: (s) => s.MQTTENABLED === true,
  MQTTCELLV: (s) => s.MQTTENABLED === true,
  MQTTHEAP: (s) => s.MQTTENABLED === true,
  REMBMSRESET: (s) => s.MQTTENABLED === true,
  HADISC: (s) => s.MQTTENABLED === true,
  HADISCFWU: (s) => s.MQTTENABLED === true,
  HADISCTOPIC: (s) => s.MQTTENABLED === true && (s.HADISC === true || s.HADISCFWU === true),
  SYSLOGIP: (s) => s.SYSLOGEN === true,
  SYSLOGPORT: (s) => s.SYSLOGEN === true,
  SYSLOGFAC: (s) => s.SYSLOGEN === true,
};

const NUMERIC_TYPES = new Set(['uint', 'int', 'float', 'seconds']);

const prettify = (key) =>
  key.trim().replace(/_/g, ' ').toLowerCase().replace(/\b\w/g, (c) => c.toUpperCase());

const labelFor = (field) => field.label ?? LABELS[field.key] ?? prettify(field.key);

// Live controls apply on the device immediately, unlike the reboot-gated schema categories.
const LIVE_CATEGORY = 'live';

const DYNAMIC_CATEGORY = 'hardware';
const TERMINATION_TITLE = 'Bus termination';
const LOAD_SWITCH_TITLE = 'Load switch';
const LOAD_SWITCH_NOTE = 'Role changes take effect after reboot; duty and divisor apply immediately on save.';

// Each control POSTs only its own key (absent fields preserve on the device).
const LIVE_SECTIONS = [
  {
    key: 'chargelimits',
    title: 'Charge limits',
    endpoint: '/api/chargelimits',
    fields: [
      { key: 'battery_wh_max', label: 'Battery capacity (Wh)', type: 'uint' },
      { key: 'use_scaled_soc', label: 'Use scaled SOC', type: 'bool' },
      { key: 'soc_max', label: 'Maximum SOC (%)', type: 'float' },
      { key: 'soc_min', label: 'Minimum SOC (%)', type: 'float' },
      { key: 'charge_a', label: 'Maximum charge current (A)', type: 'float' },
      { key: 'discharge_a', label: 'Maximum discharge current (A)', type: 'float' },
      { key: 'use_volt_limits', label: 'Use voltage limits', type: 'bool' },
      { key: 'target_ch_v', label: 'Maximum charge voltage (V)', type: 'float' },
      { key: 'target_disch_v', label: 'Minimum discharge voltage (V)', type: 'float' },
      { key: 'bms_reset_duration', label: 'BMS reset duration (s)', type: 'float' },
    ],
  },
  {
    key: 'charger',
    title: 'Charger',
    endpoint: '/api/charger',
    visibleWhen: (s) => Number(s.charger) !== 0,
    fields: [
      { key: 'hv_enabled', label: 'HV charging enabled', type: 'bool' },
      { key: 'aux12v_enabled', label: 'Aux 12V charging enabled', type: 'bool' },
      { key: 'setpoint_v', label: 'Charge voltage setpoint (V)', type: 'float' },
      { key: 'setpoint_a', label: 'Charge current setpoint (A)', type: 'float' },
      { key: 'end_a', label: 'Charge termination current (A)', type: 'float' },
    ],
  },
  {
    key: 'bydautocal',
    title: 'BYD auto-calibration',
    endpoint: '/api/bydautocal',
    fields: [
      { key: 'enabled', label: 'Auto-calibrate SOC (battery 1)', type: 'bool' },
      { key: 'drift', label: 'Drift (%) (battery 1)', type: 'uint' },
      { key: 'enabled2', label: 'Auto-calibrate SOC (battery 2)', type: 'bool' },
      { key: 'drift2', label: 'Drift (%) (battery 2)', type: 'uint' },
      { key: 'cal_target_soc', label: 'Calibration target SOC (battery 1)', type: 'float' },
      { key: 'cal_target_ah', label: 'Calibration target Ah (battery 1)', type: 'float' },
      { key: 'cal_target_soc2', label: 'Calibration target SOC (battery 2)', type: 'float' },
      { key: 'cal_target_ah2', label: 'Calibration target Ah (battery 2)', type: 'float' },
    ],
  },
  {
    key: 'recoverymode',
    title: 'Recovery mode',
    endpoint: '/api/recoverymode',
    fields: [
      // The endpoint reads {on}; the ack reports {recovery_mode}.
      { key: 'recovery_mode', label: 'Undercharged recovery mode', type: 'bool', postKey: 'on' },
    ],
  },
  {
    key: 'canidcutoff',
    title: 'CAN ID cutoff',
    endpoint: '/api/canidcutoff',
    fields: [{ key: 'cutoff', label: 'CAN ID logging cutoff', type: 'uint' }],
  },
  {
    key: 'balancing',
    title: 'Balancing',
    endpoint: '/api/balancing',
    // BMW-PHEV-specific: its BMS exposes a user-configurable balancing time limit; other batteries do not.
    visibleWhen: (s) => inList(BAT.BMWPHEV, s.battery),
    fields: [{ key: 'max_time_min', label: 'Balancing max time (min)', type: 'float' }],
  },
  {
    key: 'balancing_tesla',
    title: 'Balancing',
    endpoint: '/api/balancing',
    multiSlot: true,
    // 3/Y only: the driver's forced-balancing override never runs on S/X.
    // Bounds live server-side (maxima depend on each pack's detected chemistry).
    visibleWhen: (s) => inList(BAT.TESLA, s.battery) && Number(s.GTWCHASSIS) >= TESLA_CHASSIS_MODEL_3,
    fields: [
      { key: 'max_time_min', label: 'Balancing max time (min)', type: 'float' },
      { key: 'max_cell_mv', label: 'Max cell voltage (mV)', type: 'uint' },
      { key: 'max_dev_mv', label: 'Max cell deviation (mV)', type: 'uint' },
      { key: 'max_pack_v', label: 'Max pack voltage (V)', type: 'float' },
      { key: 'float_power_w', label: 'Float power (W)', type: 'uint' },
    ],
  },
];

const REBOOT_TO_APPLY =
  'Settings saved. Reboot now to apply them? If the emulator is handling contactors, they will open during reboot.';
const FACTORY_RESET_CONFIRM =
  'Erase ALL saved settings and restore factory defaults? This cannot be undone. The device must reboot afterwards to load the defaults.';
const FACTORY_RESET_REBOOT =
  'Factory defaults restored. Reboot now to load them? If the emulator is handling contactors, they will open during reboot.';
const FACTORY_RESET_FAILED = 'The device could not perform a factory reset.';
const PASSWORD_MISMATCH = 'Web interface passwords do not match.';
const INVALID_FIELD = 'A field is out of range or invalid — correct the highlighted field.';
const SAVE_UNREACHABLE = 'Could not reach the device to save settings.';
const SAVE_FAILED = 'The device could not save these settings.';
const HTTP_BAD_REQUEST = 400;

let root = null;
let data = null;
let state = {};
let snapshot = {};
let saveError = null;
let saveBarEl = null;
let saveErrorEl = null;
let editcards = null;
let editcardsError = false;
let dynamicState = { termination: null, loadswitch: null };
let liveErrors = {};
let dynamicSnapshot = 'null';
let activeCategory = CATEGORIES[0][0];

function controlValue(type, control) {
  if (type === 'bool') return control.checked;
  if (type === 'enum' || type === 'interface' || NUMERIC_TYPES.has(type)) return Number(control.value);
  return control.value;
}

function selectFrom(key, type, options) {
  const sel = el('select');
  sel.name = key;
  const current = String(state[key]);
  (options ?? []).forEach((opt) => {
    const value = type === 'interface' ? opt.id : opt.v;
    const o = el('option', null, type === 'interface' ? opt.name : opt.n);
    o.value = String(value);
    if (String(value) === current) o.selected = true;
    sel.append(o);
  });
  return sel;
}

function control(field) {
  const { key, type, options } = field;
  if (type === 'bool') {
    const cb = el('input');
    cb.type = 'checkbox';
    cb.name = key;
    cb.checked = state[key] === true;
    return cb;
  }
  if (type === 'enum') {
    const opts = data.options?.[options] ?? CLIENT_OPTIONS[options];
    if (!opts) return el('span', 'settings-error', `Missing options: ${options}`);
    return selectFrom(key, type, opts);
  }
  if (type === 'interface') return selectFrom(key, type, data.interfaces);

  const input = el('input');
  input.name = key;
  if (PASSWORD_KEYS.has(key)) input.type = 'password';
  else if (NUMERIC_TYPES.has(type)) input.type = 'number';
  else input.type = 'text';
  if (input.type === 'number') {
    if (type === 'float') input.step = 'any';
    if (field.min != null) input.min = String(field.min);
    if (field.max != null) input.max = String(field.max);
  } else if (PATTERNS[key]) {
    input.pattern = PATTERNS[key];
  }
  if (input.type !== 'number' && field.max != null) input.maxLength = field.max;
  const placeholder = data.placeholders?.[key];
  if (placeholder) input.placeholder = placeholder;
  input.value = state[key] ?? '';
  return input;
}

function fieldRow(field) {
  const row = el('div', 'field');
  row.dataset.key = field.key;
  const ctrl = control(field);
  ctrl.id = field.key;
  if (HELP[field.key]) ctrl.title = HELP[field.key];
  ctrl.addEventListener('change', () => onControlChange(field, ctrl));
  const cell = CLEARABLE_PASSWORD_KEYS.has(field.key) ? passwordCell(field, ctrl) : ctrl;
  const label = el('label', null, labelFor(field));
  label.htmlFor = field.key;
  row.append(label, cell);
  return row;
}

// The "Clear" toggle sends an explicit null to erase the stored secret; a blank value only keeps it.
function passwordCell(field, input) {
  const cell = el('div', 'field-pw');
  const clear = el('label', 'field-clear');
  const cb = el('input');
  cb.type = 'checkbox';
  cb.checked = state[field.key] === null;  // survive a panel re-render with the clear still pending
  input.disabled = cb.checked;
  cb.addEventListener('change', () => {
    state[field.key] = cb.checked ? null : '';
    input.disabled = cb.checked;
    if (cb.checked) input.value = '';
    if (field.key === 'HTTPPASS') {
      state.HTTPPASSCONFIRM = cb.checked ? null : '';
      const confirm = cell.closest('.settings-panel')?.querySelector('input[name="HTTPPASSCONFIRM"]');
      if (confirm) {
        confirm.disabled = cb.checked;
        if (cb.checked) confirm.value = '';
      }
    }
    refreshSaveBar();
  });
  clear.append(cb, el('span', null, 'Clear'));
  cell.append(input, clear);
  return cell;
}

function applyVisibility(panel) {
  panel.querySelectorAll('.field[data-key]').forEach((row) => {
    const rule = VISIBILITY[row.dataset.key];
    row.classList.toggle('hidden', rule ? !rule(state) : false);
  });
}

// Enabling one general logging path clears the other (one path performs better).
function enforceLogExclusion(key, panel) {
  const isLogToggle = key === 'USBENABLED' || key === 'WEBENABLED';
  if (!isLogToggle || state[key] !== true) return;
  const other = key === 'USBENABLED' ? 'WEBENABLED' : 'USBENABLED';
  state[other] = false;
  const cb = panel.querySelector(`input[name="${other}"]`);
  if (cb) cb.checked = false;
}

function onControlChange(field, ctrl) {
  // Number('') is 0; preserve the prior value rather than silently writing 0.
  const isNumeric = NUMERIC_TYPES.has(field.type);
  if (!isNumeric || (ctrl.value !== '' && !Number.isNaN(Number(ctrl.value)))) {
    state[field.key] = controlValue(field.type, ctrl);
  }
  const panel = ctrl.closest('.settings-panel');
  enforceLogExclusion(field.key, panel);
  applyVisibility(panel);
  refreshSaveBar();
}

function buildPanel() {
  const panel = el('div', 'settings-panel');
  (data.schema ?? [])
    .filter((field) => field.category === activeCategory)
    .forEach((field) => {
      panel.append(fieldRow(field));
      if (field.key === 'HTTPPASS') {
        panel.append(fieldRow({ key: 'HTTPPASSCONFIRM', type: 'string' }));
      }
    });
  if (activeCategory === DYNAMIC_CATEGORY) appendDynamicControls(panel);
  applyVisibility(panel);
  return panel;
}

function dynSelectRow(label, options, current, onSet) {
  const row = el('div', 'field');
  const sel = el('select');
  (options ?? []).forEach((opt) => {
    const o = el('option', null, opt.n);
    o.value = String(opt.v);
    if (Number(opt.v) === Number(current)) o.selected = true;
    sel.append(o);
  });
  sel.addEventListener('change', () => {
    onSet(Number(sel.value));
    refreshSaveBar();
  });
  row.append(el('label', null, label), sel);
  return row;
}

function dynNumberRow(label, current, onSet) {
  const row = el('div', 'field');
  const input = el('input');
  input.type = 'number';
  input.value = current ?? '';
  input.addEventListener('change', () => {
    // Empty/NaN preserves the model value rather than writing 0 on save.
    if (input.value !== '' && !Number.isNaN(Number(input.value))) onSet(Number(input.value));
    refreshSaveBar();
  });
  row.append(el('label', null, label), input);
  return row;
}

function buildTerminationSection() {
  const wrap = el('div', 'settings-subsection');
  wrap.append(el('h3', null, TERMINATION_TITLE));
  dynamicState.termination.forEach((entry) => {
    const row = el('div', 'field');
    const cb = el('input');
    cb.type = 'checkbox';
    cb.checked = entry.enabled === true;
    cb.title = 'Switch the 120 Ω termination resistor onto the bus';
    cb.addEventListener('change', () => {
      entry.enabled = cb.checked;
      refreshSaveBar();
    });
    row.append(el('label', null, `${entry.name} termination`), cb);
    wrap.append(row);
  });
  return wrap;
}

function buildLoadSwitchSection() {
  const wrap = el('div', 'settings-subsection');
  wrap.append(el('h3', null, LOAD_SWITCH_TITLE), el('div', 'muted', LOAD_SWITCH_NOTE));
  const roles = data.options?.loadswitchrole ?? [];
  const divisors = data.dynamic?.loadswitch?.divisors ?? [];
  dynamicState.loadswitch.channels.forEach((ch) => {
    const group = el('div', 'settings-channel-config');
    group.append(el('h4', null, `SW${ch.channel}`));
    group.append(
      dynSelectRow('Role', roles, ch.role, (v) => { ch.role = v; }),
      dynNumberRow('Steady-state duty (%)', ch.duty, (v) => { ch.duty = v; }),
      dynSelectRow('PWM divisor', divisors, ch.divisor, (v) => { ch.divisor = v; }),
    );
    wrap.append(group);
  });
  return wrap;
}

function appendDynamicControls(panel) {
  if (dynamicState.termination) panel.append(buildTerminationSection());
  if (dynamicState.loadswitch) panel.append(buildLoadSwitchSection());
}

// Keeps the render-only `name`; buildDynamic drops it back to the shapes apply_settings_json expects.
function seedDynamic(dyn) {
  dynamicState = { termination: null, loadswitch: null };
  if (dyn?.termination) {
    dynamicState.termination = dyn.termination.map((t) => ({
      index: t.index,
      name: t.name,
      enabled: t.enabled === true,
    }));
  }
  if (dyn?.loadswitch?.channels) {
    dynamicState.loadswitch = {
      channels: dyn.loadswitch.channels.map((c) => ({
        channel: c.channel,
        role: Number(c.role),
        duty: Number(c.duty),
        divisor: Number(c.divisor),
      })),
    };
  }
  dynamicSnapshot = JSON.stringify(dynamicState);
}

// The applier null-guards each sub-field, so emitting only changed-vs-snapshot fields
// applies exactly those; sending everything re-quantises duty on unrelated saves.
function buildDynamic() {
  const base = JSON.parse(dynamicSnapshot);
  const out = {};
  if (dynamicState.termination) {
    const changed = dynamicState.termination
      .filter((t) => {
        const b = base?.termination?.find((x) => x.index === t.index);
        return !b || b.enabled !== t.enabled;
      })
      .map((t) => ({ index: t.index, enabled: t.enabled }));
    if (changed.length) out.termination = changed;
  }
  if (dynamicState.loadswitch) {
    const channels = [];
    dynamicState.loadswitch.channels.forEach((c) => {
      const b = base?.loadswitch?.channels?.find((x) => x.channel === c.channel);
      const entry = { channel: c.channel };
      if (!b || b.role !== c.role) entry.role = c.role;
      if (!b || b.duty !== c.duty) entry.duty = c.duty;
      if (!b || b.divisor !== c.divisor) entry.divisor = c.divisor;
      if (Object.keys(entry).length > 1) channels.push(entry);
    });
    if (channels.length) out.loadswitch = { channels };
  }
  return out;
}

function navChip(id, name) {
  const chip = el('button', 'settings-chip', name);
  chip.type = 'button';
  if (id === activeCategory) chip.setAttribute('aria-current', 'true');
  chip.addEventListener('click', () => {
    if (id === activeCategory) return;
    activeCategory = id;
    renderShell();
  });
  return chip;
}

function buildNav() {
  const nav = el('div', 'settings-nav');
  CATEGORIES.forEach(([id, name]) => nav.append(navChip(id, name)));
  nav.append(navChip(LIVE_CATEGORY, 'Live controls'));
  return nav;
}

function liveControl(field, current) {
  if (field.type === 'bool') {
    const cb = el('input');
    cb.type = 'checkbox';
    cb.checked = current === true;
    return cb;
  }
  const input = el('input');
  input.type = 'number';
  if (field.type === 'float') input.step = 'any';
  input.value = current ?? '';
  return input;
}

const LIVE_REJECTED = 'The device rejected this value.';

// Repaints only this section from the returned ack: the UI must reflect what
// the device applied, not what was typed.
async function onLiveChange(section, field, ctrl) {
  const repaintSection = () => {
    const existing = root.querySelector(`.settings-live-section[data-section="${section.key}"]`);
    if (existing) existing.replaceWith(buildLiveSection(section));
  };
  let value;
  if (field.type === 'bool') {
    value = ctrl.checked;
  } else {
    // A cleared number input parses to 0; skip the write (and restore the device
    // value) so clearing a field to retype it never live-writes 0.
    value = Number(ctrl.value);
    if (ctrl.value === '' || Number.isNaN(value)) {
      repaintSection();
      return;
    }
  }
  ctrl.disabled = true;
  try {
    const body = { [field.postKey ?? field.key]: value };
    if (section.slot !== undefined) body.battery = section.slot;
    storeLiveAck(section, await postJson(section.endpoint, body));
    delete liveErrors[section.key];
  } catch (e) {
    // The change did not take; re-render restores the device's last-known
    // values and shows why it was refused.
    liveErrors[section.key] = e?.body || LIVE_REJECTED;
  } finally {
    ctrl.disabled = false;
  }
  repaintSection();
}

function liveValues(section) {
  if (section.arrayKey === undefined) return editcards?.[section.key] ?? {};
  return editcards?.[section.arrayKey]?.find((e) => e.slot === section.slot) ?? {};
}

function storeLiveAck(section, ack) {
  if (section.arrayKey === undefined) {
    editcards[section.key] = ack;
    return;
  }
  const entries = editcards[section.arrayKey];
  const idx = entries.findIndex((e) => e.slot === section.slot);
  if (idx >= 0) entries[idx] = { slot: section.slot, ...ack };
}

// A multiSlot section renders one card per entry in its editcards array —
// the firmware serves entries only for slots a POST would accept.
function liveCardInstances() {
  return LIVE_SECTIONS.filter((section) => !section.visibleWhen || section.visibleWhen(state)).flatMap((section) => {
    if (!section.multiSlot) return [section];
    const entries = editcards?.[section.key];
    if (!Array.isArray(entries)) return [];
    return entries.map(({ slot }) => ({
      ...section,
      slot,
      arrayKey: section.key,
      key: slot === 0 ? section.key : `${section.key}:${slot}`,
      title: slot === 0 ? section.title : `${section.title} (battery ${slot + 1})`,
    }));
  });
}

function buildLiveSection(section) {
  const wrap = el('div', 'settings-live-section');
  wrap.dataset.section = section.key;
  wrap.append(el('h3', null, section.title));
  if (liveErrors[section.key]) wrap.append(el('div', 'settings-error', liveErrors[section.key]));
  const values = liveValues(section);
  section.fields.forEach((field) => {
    const row = el('div', 'field');
    const ctrl = liveControl(field, values[field.key]);
    row.append(el('label', null, field.label), ctrl);
    ctrl.addEventListener('change', () => onLiveChange(section, field, ctrl));
    wrap.append(row);
  });
  return wrap;
}

function buildLivePanel() {
  const panel = el('div', 'settings-panel settings-live');
  panel.append(el('div', 'settings-live-note', 'These controls apply immediately — no reboot required.'));
  if (editcardsError || !editcards) {
    panel.append(el('div', 'settings-error', 'Could not load live control values. The device may be busy — try again.'));
    return panel;
  }
  liveCardInstances().forEach((card) => panel.append(buildLiveSection(card)));
  return panel;
}

function buildValues() {
  const values = {};
  (data.schema ?? []).forEach((field) => {
    values[field.key] = state[field.key];
  });
  // Absent = preserve, so gather from state (never the DOM): an orphan value not
  // in a select's options would otherwise be lost to the browser's first-option default.
  values.HTTPPASSCONFIRM = state.HTTPPASSCONFIRM ?? '';
  return values;
}

function isDirty() {
  if (Object.keys(snapshot).some((key) => state[key] !== snapshot[key])) return true;
  return JSON.stringify(dynamicState) !== dynamicSnapshot;
}

function refreshSaveBar() {
  if (saveBarEl) saveBarEl.classList.toggle('hidden', !isDirty());
}

function setSaveError(message) {
  saveError = message;
  if (saveErrorEl) {
    saveErrorEl.textContent = message ?? '';
    saveErrorEl.classList.toggle('hidden', !message);
  }
}

function seedState(values) {
  state = { ...(values ?? {}) };
  // Seed the confirm field once so the match check is meaningful, not vacuous.
  state.HTTPPASSCONFIRM = state.HTTPPASS ?? '';
}

// First applicable field whose value violates its pattern or numeric bounds, across every
// category. Patterns are client-only (the server range-rejects numbers but only type-checks
// strings), and a value edited then navigated away from survives in state, so a DOM-only check
// on the rendered panel would let it through. VISIBILITY-gated-off fields are not applicable.
function firstInvalidField() {
  for (const field of data.schema ?? []) {
    const rule = VISIBILITY[field.key];
    if (rule && !rule(state)) continue;
    const value = state[field.key];
    const pattern = PATTERNS[field.key];
    if (pattern) {
      if (typeof value === 'string' && value !== '' && !new RegExp(`^(?:${pattern})$`).test(value)) return field;
    } else if (typeof value === 'number') {
      if ((field.min != null && value < field.min) || (field.max != null && value > field.max)) return field;
    }
  }
  return null;
}

async function onSave() {
  // A pending clear (null) has no password to confirm, so the match check does not apply.
  if (state.HTTPPASS !== null && String(state.HTTPPASS ?? '') !== String(state.HTTPPASSCONFIRM ?? '')) {
    setSaveError(PASSWORD_MISMATCH);
    return;
  }
  const bad = firstInvalidField();
  if (bad) {
    if (bad.category !== activeCategory) {
      activeCategory = bad.category;
      renderShell();
    }
    setSaveError(INVALID_FIELD);
    root.querySelector(`.settings-panel input[name="${bad.key}"]`)?.reportValidity();
    return;
  }
  setSaveError(null);
  const body = { values: buildValues(), dynamic: buildDynamic() };
  let res;
  try {
    res = await fetch('/api/settings', {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
  } catch {
    setSaveError(SAVE_UNREACHABLE);
    return;
  }
  // Body reads stay inside the try: a truncated/non-JSON body must surface as an
  // error, not reject silently and leave the bar dirty with no explanation.
  try {
    if (res.status === HTTP_BAD_REQUEST) {
      setSaveError(await res.text());
      return;
    }
    if (!res.ok) {
      setSaveError(SAVE_FAILED);
      return;
    }
    const result = await res.json();
    data = result;
    seedState(result.values);
    seedDynamic(result.dynamic);
    snapshot = { ...state };
    setSaveError(null);
    renderShell();
    if (result.meta?.reboot_required === true && window.confirm(REBOOT_TO_APPLY)) {
      fetch('/reboot');
    }
  } catch {
    setSaveError(SAVE_FAILED);
  }
}

function buildSaveBar() {
  const bar = el('div', 'settings-savebar');
  bar.classList.toggle('hidden', !isDirty());
  const err = el('div', 'settings-savebar-error');
  err.textContent = saveError ?? '';
  err.classList.toggle('hidden', !saveError);
  const message = el('span', 'settings-savebar-msg', 'Unsaved changes');
  const save = el('button', 'btn', 'Save & apply');
  save.type = 'button';
  save.addEventListener('click', onSave);
  bar.append(err, message, save);
  saveBarEl = bar;
  saveErrorEl = err;
  return bar;
}

function buildDangerZone() {
  const zone = el('div', 'settings-danger-zone');
  const err = el('div', 'settings-error');
  err.hidden = true;
  const btn = el('button', 'btn btn-fault', 'Factory reset');
  btn.type = 'button';
  btn.addEventListener('click', async () => {
    if (!window.confirm(FACTORY_RESET_CONFIRM)) return;
    err.textContent = '';
    err.hidden = true;
    const fail = () => {
      err.textContent = FACTORY_RESET_FAILED;
      err.hidden = false;
    };
    let res;
    try {
      res = await fetch('/api/factoryreset', { method: 'POST', credentials: 'same-origin' });
    } catch {
      fail();
      return;
    }
    if (!res.ok) {
      fail();
      return;
    }
    // Reset clears NVS but does not reboot; defaults only load after a restart.
    if (window.confirm(FACTORY_RESET_REBOOT)) {
      fetch('/reboot');
    } else {
      location.reload();  // NVS is cleared; re-read so a later Save can't repaint stale values
    }
  });
  zone.append(btn, err);
  return zone;
}

function renderShell() {
  const panel = activeCategory === LIVE_CATEGORY ? buildLivePanel() : buildPanel();
  root.replaceChildren(buildNav(), panel, buildSaveBar(), buildDangerZone());
}

export async function mount(container) {
  root = container;
  try {
    data = await getJson('/api/settings');
  } catch {
    root.replaceChildren(el('div', 'settings-error', 'Could not load settings. The device may be busy — try again.'));
    return;
  }
  seedState(data.values);
  seedDynamic(data.dynamic);
  snapshot = { ...state };
  setSaveError(null);
  liveErrors = {};
  activeCategory = CATEGORIES[0][0];
  try {
    editcards = await getJson('/api/editcards');
    editcardsError = false;
  } catch {
    editcards = null;
    editcardsError = true;
  }
  renderShell();
}

// Settings loads once in mount, not from the /api/state poll, so a repaint from
// the poll must not disturb the form or the live panel's in-progress edits.
export function render() {}
