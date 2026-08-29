import {
  getJson, postJson, skinName, t, tf, currentLanguage, cachedLanguages, setLanguage,
} from '/app.js';

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

const CATEGORIES = [
  ['network', t('category.network', 'Network')],
  ['webauth', t('category.webauth', 'Web access')],
  ['battery', t('category.battery', 'Battery')],
  ['inverter', t('category.inverter', 'Inverter')],
  ['optional', t('category.optional', 'Optional')],
  ['hardware', t('category.hardware', 'Hardware')],
  ['connectivity', t('category.connectivity', 'Connectivity')],
  ['debug', t('category.debug', 'Debug')],
  ['interface', t('category.interface', 'Interface')],
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
  WEBUI: 'Web interface style',
  WEBAUTH: 'Enable password protection',
  HTTPUSER: 'Username',
  HTTPPASS: 'Web interface password',
  HTTPPASSCONFIRM: 'Repeat web interface password',
  SSID: 'SSID',
  PASSWORD: 'Password',
  BATTCHEM: 'Battery chemistry',
  INVTYPE: 'Inverter protocol',
  INVCOMM: 'Inverter interface',
  INVOFFGRID: 'Inverter run entirely offgrid',
  LOWPASSFILTER: 'Ramp up charge limits gradually',
  CHGTAPERSOC: 'Charge power tapering based on SOC',
  CHGTAPERSTART: 'Start tapering at SOC, percent',
  CHGTAPERFLOOR: 'Float charge power, W',
  SLOWCANINV: 'Allow longer CAN timeout',
  CHGTYPE: 'Charger',
  CHGCOMM: 'Charger interface',
  SHUNTTYPE: 'Shunt',
  SHUNTCOMM: 'Shunt interface',
  CTOFFSET: 'CT Clamp offset (mV)',
  CTVNOM: 'CT Clamp nominal voltage (dV)',
  CTANOM: 'CT Clamp nominal current (A)',
  CTATTEN: 'ESP32 pin attenuation',
  CTINVERT: 'Invert CT current',
  CANFDASCAN: 'Use CanFD as classic CAN',
  CANFD2ASCAN: 'Use CanFD2 as classic CAN',
  EQSTOP: 'Equipment stop button',
  PRECHGMS: 'Precharge time ms',
  NCCONTACTOR: 'Use Normally Closed logic',
  PWMCNTCTRL: 'PWM contactor control',
  PWMFREQ: 'PWM Frequency Hz',
  PWMHOLD: 'PWM Hold 1-1023',
  PERBMSRESET: 'Periodic BMS reset',
  PERBMSRESETH: 'Reset interval',
  PERBMSDEFSOC: 'Defer reset if SOC less than 15%',
  PERBMSSKIPBAL: 'Skip reset for one period if balancing',
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
  MEASURECPUTEMP: 'Measure CPU temperature',
  CPUTEMPOFFSET: 'CPU temperature calibration offset (°C)',
  CANLOGUSB: 'Enable CAN message logging via USB serial',
  USBENABLED: 'Enable general logging via USB serial',
  WEBENABLED: 'Enable general logging via Webserver',
  CANLOGSD: 'Enable CAN message logging via SD card',
  SDLOGENABLED: 'Enable general logging via SD card',
  SYSLOGEN: 'General logging to syslog server',
  SYSLOGIP: 'Syslog server IP',
  SYSLOGPORT: 'Syslog UDP port',
  SYSLOGFAC: 'Syslog facility',
  BATTERY_WH_MAX: 'Battery capacity (Wh)',
  USE_SCALED_SOC: 'Use scaled SOC',
  MAXPERCENTAGE: 'Maximum SOC (%)',
  MINPERCENTAGE: 'Minimum SOC (%)',
  MAXCHARGEAMP: 'Maximum charge current (A)',
  MAXDISCHARGEAMP: 'Maximum discharge current (A)',
  USEVOLTLIMITS: 'Use voltage limits',
  TARGETCHVOLT: 'Maximum charge voltage (V)',
  TARGETDISCHVOLT: 'Minimum discharge voltage (V)',
  BMSRESETDUR: 'BMS reset duration (s)',
  hv_enabled: 'HV charging enabled',
  aux12v_enabled: 'Aux 12V charging enabled',
  setpoint_v: 'Charge voltage setpoint (V)',
  setpoint_a: 'Charge current setpoint (A)',
  end_a: 'Charge termination current (A)',
  recovery_mode: 'Undercharged recovery mode',
  cutoff: 'CAN ID logging cutoff',
};

// Client-only text-field regexes (numeric bounds ride the schema). An empty value passes
// natively, so optional blank fields (unset IPs, hostname, anonymous MQTT) still save.
const IPV4_PATTERN = '((25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(25[0-5]|2[0-4]\\d|1?\\d?\\d)';
const HELP = {
  HADISC: t('help.HADISC',
    'Publish the discovery configs once after the next restart. Clears itself once they have been published.'),
  HADISCFWU: t('help.HADISCFWU',
    'Publish the discovery configs once after every firmware update. They carry the software version and can gain or change entities between releases.'),
  HADISCTOPIC: t('help.HADISCTOPIC', "MQTT auto discovery base topic (letters, numbers, '_', '-')"),
  ESPNOWENABLED: t('help.ESPNOWENABLED', 'Send battery telemetry to nearby devices over ESP-NOW'),
  ESPNOWMACS: t('help.ESPNOWMACS',
    'Comma separated list of receiver MAC addresses, e.g. AA:BB:CC:DD:EE:FF, 11:22:33:44:55:66 (max 8). Leave empty to broadcast to every device. Takes effect after a restart.'),
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

const RELOAD_ON_CHANGE = ['WEBUI'];

const PASSWORD_KEYS = new Set(['PASSWORD', 'HTTPPASS', 'HTTPPASSCONFIRM', 'APPASSWORD', 'MQTTPASSWORD']);
// Stored secrets that can be explicitly cleared (sent as JSON null). HTTPPASSCONFIRM is not stored.
const CLEARABLE_PASSWORD_KEYS = new Set(['PASSWORD', 'HTTPPASS', 'APPASSWORD', 'MQTTPASSWORD']);

const SHUNT_CTCLAMP = 3;
const TESLA_CHASSIS_MODEL_3 = 2;

const fieldFor = (key) => (data?.schema ?? []).find((f) => f.key === key);

const ownedBy = (field) => {
  if (!field?.owners) return true;
  if (field.domain === 'inverter') return field.owners.includes(Number(state.INVTYPE));
  const slots = dynamicState.batteries ?? [];
  if (field.slot != null) {
    const entry = slots.find((b) => b.slot === field.slot);
    return entry != null && field.owners.includes(Number(entry.type));
  }
  return slots.some((b) => field.owners.includes(Number(b.type)));
};

const forcesBalancing = () => ownedBy(fieldFor('max_cell_mv'));

// 3/Y only: the driver's forced-balancing override never runs on S/X.
const forcedBalancing = (s) => forcesBalancing() && Number(s.GTWCHASSIS) >= TESLA_CHASSIS_MODEL_3;

// A driver owning the balancing time but not the chassis-gated rows carries no chassis condition.
// Testing the chassis alone would hide the row for a BMW PHEV sharing the box with a sub-3/Y Tesla.
const ownsUngatedBalancing = () => {
  const gated = fieldFor('max_cell_mv')?.owners ?? [];
  const ungated = (fieldFor('max_time_min')?.owners ?? []).filter((id) => !gated.includes(id));
  return (dynamicState.batteries ?? []).some((b) => ungated.includes(Number(b.type)));
};

// Value-driven show/hide; ownership and board-gated absence are handled by the schema.
const VISIBILITY = {
  BATTCHEM: (s) => Number(s.battery) !== 0,
  INVCOMM: (s) => Number(s.INVTYPE) !== 0,
  INVOFFGRID: (s) => Number(s.INVTYPE) !== 0,
  PERBMSRESETH: (s) => s.PERBMSRESET === true,
  PERBMSDEFSOC: (s) => s.PERBMSRESET === true,
  PERBMSSKIPBAL: (s) => s.PERBMSRESET === true,
  CPUTEMPOFFSET: (s) => s.MEASURECPUTEMP === true,
  LOWPASSFILTER: (s) => Number(s.INVTYPE) !== 0,
  CHGTAPERSOC: (s) => Number(s.INVTYPE) !== 0,
  CHGTAPERSTART: (s) => Number(s.INVTYPE) !== 0 && s.CHGTAPERSOC === true,
  CHGTAPERFLOOR: (s) => Number(s.INVTYPE) !== 0 && s.CHGTAPERSOC === true,
  SLOWCANINV: (s) => Number(s.INVTYPE) !== 0,
  CHGCOMM: (s) => Number(s.CHGTYPE) !== 0,
  SHUNTCOMM: (s) => Number(s.SHUNTTYPE) !== 0 && Number(s.SHUNTTYPE) !== SHUNT_CTCLAMP,
  CTOFFSET: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTVNOM: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTANOM: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTATTEN: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
  CTINVERT: (s) => Number(s.SHUNTTYPE) === SHUNT_CTCLAMP,
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
  hv_enabled: (s) => Number(s.CHGTYPE) !== 0,
  aux12v_enabled: (s) => Number(s.CHGTYPE) !== 0,
  setpoint_v: (s) => Number(s.CHGTYPE) !== 0,
  setpoint_a: (s) => Number(s.CHGTYPE) !== 0,
  end_a: (s) => Number(s.CHGTYPE) !== 0,
  max_time_min: (s) => ownsUngatedBalancing() || forcedBalancing(s),
  max_cell_mv: forcedBalancing,
  max_dev_mv: forcedBalancing,
  max_pack_v: forcedBalancing,
  float_power_w: forcedBalancing,
};

const NUMERIC_TYPES = new Set(['uint', 'int', 'float', 'seconds']);

const prettify = (key) =>
  key.trim().replace(/_/g, ' ').toLowerCase().replace(/\b\w/g, (c) => c.toUpperCase());

const labelFor = (field) => t(`setting.${field.key}`, field.label ?? LABELS[field.key] ?? prettify(field.key));

// Live controls apply on the device immediately, unlike the reboot-gated schema categories.
const SETTINGS_SKINS = {
  modern: { contiguous: false },
  legacy: { contiguous: true },
};

const skin = SETTINGS_SKINS[skinName] ?? SETTINGS_SKINS.modern;

const LIVE_CATEGORY = 'live';

const DYNAMIC_CATEGORY = 'hardware';
const INTERFACE_CATEGORY = 'interface';
const SOURCE_LANGUAGE = 'en';
const BATTERY_CATEGORY = 'battery';
const BATTERIES_TITLE = t('ui.batteries', 'Batteries');
const MAX_BATTERY_SLOTS = 3;
const TERMINATION_TITLE = t('ui.bus_termination', 'Bus termination');
const LOAD_SWITCH_TITLE = t('ui.load_switch', 'Load switch');
const LOAD_SWITCH_NOTE =
  t('ui.load_switch_note',
    'Role changes take effect after reboot; duty and divisor apply immediately on save.');

const LIVE_SECTION_TITLES = {
  chargelimits: t('live.chargelimits', 'Charge limits'),
  charger: t('live.charger', 'Charger'),
  bydautocal: t('live.bydautocal', 'BYD auto-calibration'),
  recoverymode: t('live.recoverymode', 'Recovery mode'),
  canidcutoff: t('live.canidcutoff', 'CAN ID cutoff'),
  balancing: t('live.balancing', 'Balancing'),
};

const REBOOT_TO_APPLY =
  t('ui.reboot_to_apply',
    'Settings saved. Reboot now to apply them? If the emulator is handling contactors, they will open during reboot.');
const FACTORY_RESET_CONFIRM =
  t('ui.factory_reset_confirm',
    'Erase ALL saved settings and restore factory defaults? This cannot be undone. The device must reboot afterwards to load the defaults.');
const FACTORY_RESET_REBOOT =
  t('ui.factory_reset_reboot',
    'Factory defaults restored. Reboot now to load them? If the emulator is handling contactors, they will open during reboot.');
const FACTORY_RESET_FAILED = t('ui.factory_reset_failed', 'The device could not perform a factory reset.');
const PASSWORD_MISMATCH = t('error.webauth_password_mismatch', 'Web interface passwords do not match.');
const INVALID_FIELD =
  t('ui.invalid_field', 'A field is out of range or invalid — correct the highlighted field.');
const SAVE_UNREACHABLE = t('ui.save_unreachable', 'Could not reach the device to save settings.');
const SAVE_FAILED = t('ui.save_failed', 'The device could not save these settings.');
const HTTP_BAD_REQUEST = 400;

let root = null;
let data = null;
let state = {};
let snapshot = {};
let saveError = null;
let saveBarEl = null;
let saveErrorEl = null;
let dynamicState = { termination: null, loadswitch: null, batteries: null };
let liveErrors = {};
let dynamicSnapshot = 'null';
let activeCategory = CATEGORIES[0][0];

function controlValue(type, control) {
  if (type === 'bool') return control.checked;
  if (type === 'enum' || type === 'interface' || NUMERIC_TYPES.has(type)) return Number(control.value);
  return control.value;
}

function selectFrom(key, type, options, ns) {
  const sel = el('select');
  sel.name = key;
  const current = String(state[key]);
  (options ?? []).forEach((opt) => {
    const value = type === 'interface' ? opt.id : opt.v;
    const o = el('option', null,
                 type === 'interface' ? opt.name : t(`${ns}.${opt.v}`, opt.n ?? prettify(String(opt.v))));
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
  if (type === 'interface') return selectFrom(key, type, data.interfaces);
  if (options) {
    const opts = data.options?.[options] ?? CLIENT_OPTIONS[options];
    if (!opts) return el('span', 'settings-error', tf('ui.missing_options', 'Missing options: {}', options));
    return selectFrom(key, type, opts, options);
  }

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
  clear.append(cb, el('span', null, t('ui.clear', 'Clear')));
  cell.append(input, clear);
  return cell;
}

function applyVisibility(panel) {
  panel.querySelectorAll('.field[data-key]').forEach((row) => {
    row.classList.toggle('hidden', !isVisible(row.dataset.key));
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

function buildPanel(category) {
  const panel = el('div', 'settings-panel');
  if (category === BATTERY_CATEGORY && dynamicState.batteries) panel.append(buildBatteriesSection());
  (data.schema ?? [])
    .filter((field) => field.category === category)
    .forEach((field) => {
      panel.append(fieldRow(field));
      if (field.key === 'HTTPPASS') {
        panel.append(fieldRow({ key: 'HTTPPASSCONFIRM', type: 'string' }));
      }
    });
  if (category === DYNAMIC_CATEGORY) appendDynamicControls(panel);
  if (category === INTERFACE_CATEGORY) appendLanguageControl(panel);
  applyVisibility(panel);
  return panel;
}

function appendLanguageControl(panel) {
  const row = el('div', 'field');
  const sel = el('select');
  sel.id = 'LANGUAGE';
  const label = el('label', null, t('ui.language', 'Language'));
  label.htmlFor = sel.id;
  const populate = () => {
    const active = currentLanguage();
    sel.replaceChildren();
    for (const entry of [{ code: SOURCE_LANGUAGE, name: 'English' }, ...cachedLanguages()]) {
      const option = el('option', null, entry.name);
      option.value = entry.code;
      if (entry.code === active) option.selected = true;
      sel.append(option);
    }
  };
  populate();
  sel.addEventListener('change', () => setLanguage(sel.value));
  window.addEventListener('be-languages-changed', () => {
    if (sel.isConnected) populate();
  });
  row.append(label, sel);

  const note = el('div', 'field');
  note.append(el('span'),
              el('span', 'muted field-note',
                 t('ui.language_note', 'Languages download automatically when this browser is online.')));
  panel.append(row, note);
}

function dynSelectRow(label, options, current, onSet, ns) {
  const row = el('div', 'field');
  const sel = el('select');
  (options ?? []).forEach((opt) => {
    const o = el('option', null, ns ? t(`${ns}.${opt.v}`, opt.n) : opt.n);
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

function syncBatteryShim() {
  const primary = dynamicState.batteries?.find((b) => b.slot === 0);
  if (!primary) return;
  state.battery = primary.type;
  state.CNTCTRL = primary.contactor_control;
}

function onBatterySlotChange() {
  syncBatteryShim();
  const panel = root.querySelector('.settings-panel');
  const existing = panel?.querySelector('[data-section="batteries"]');
  if (existing) existing.replaceWith(buildBatteriesSection());
  if (panel) applyVisibility(panel);
  refreshSaveBar();
}

function buildBatteriesSection() {
  const wrap = el('div', 'settings-subsection');
  wrap.dataset.section = 'batteries';
  wrap.append(el('h3', null, BATTERIES_TITLE));
  const types = data.options?.battery ?? [];
  const interfaces = (data.interfaces ?? []).map((iface) => ({ v: iface.id, n: iface.name }));
  dynamicState.batteries.forEach((b, i) => {
    const prev = i > 0 ? dynamicState.batteries[i - 1] : null;
    if (i > 0 && b.type === 0 && (!prev || prev.type === 0)) return;
    const card = el('div', 'settings-channel-config');
    card.append(el('h4', null, tf('ui.battery_n', 'Battery {}', b.slot + 1)));
    const slotTypes = types.filter((t) => t.v === 0 || (t.s ?? MAX_BATTERY_SLOTS) > b.slot);
    card.append(dynSelectRow(t('ui.type', 'Type'), slotTypes, b.type, (v) => {
      b.type = v;
      if (v === 0) b.contactor_control = false;
      onBatterySlotChange();
    }));
    if (b.type !== 0) {
      if (interfaces.length) {
        card.append(dynSelectRow(t('ui.interface', 'Interface'), interfaces, b.comm, (v) => { b.comm = v; }));
      }
      const row = el('div', 'field');
      const cb = el('input');
      cb.type = 'checkbox';
      cb.checked = b.contactor_control === true;
      cb.addEventListener('change', () => {
        b.contactor_control = cb.checked;
        onBatterySlotChange();
      });
      row.append(el('label', null, t('ui.contactor_control_gpio', 'Contactor control via GPIO')), cb);
      card.append(row);
    }
    wrap.append(card);
  });
  return wrap;
}

function buildTerminationSection() {
  const wrap = el('div', 'settings-subsection');
  wrap.append(el('h3', null, TERMINATION_TITLE));
  dynamicState.termination.forEach((entry) => {
    const row = el('div', 'field');
    const cb = el('input');
    cb.type = 'checkbox';
    cb.checked = entry.enabled === true;
    cb.title = t('ui.termination_help', 'Switch the 120 Ω termination resistor onto the bus');
    cb.addEventListener('change', () => {
      entry.enabled = cb.checked;
      refreshSaveBar();
    });
    row.append(el('label', null, tf('ui.interface_termination', '{} termination', entry.name)), cb);
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
      dynSelectRow(t('ui.role', 'Role'), roles, ch.role, (v) => { ch.role = v; }, 'loadswitchrole'),
      dynNumberRow(t('ui.steady_state_duty', 'Steady-state duty (%)'), ch.duty, (v) => { ch.duty = v; }),
      dynSelectRow(t('ui.pwm_divisor', 'PWM divisor'), divisors, ch.divisor, (v) => { ch.divisor = v; }),
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
  dynamicState = { termination: null, loadswitch: null, batteries: null };
  if (Array.isArray(dyn?.batteries)) {
    dynamicState.batteries = dyn.batteries.map((b) => ({
      slot: b.slot,
      type: Number(b.type),
      comm: Number(b.comm),
      contactor_control: b.contactor_control === true,
    }));
  }
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
  syncBatteryShim();
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
  if (dynamicState.batteries) {
    const changed = dynamicState.batteries
      .filter((b) => {
        const s = base?.batteries?.find((x) => x.slot === b.slot);
        return !s || s.type !== b.type || s.comm !== b.comm || s.contactor_control !== b.contactor_control;
      })
      .map((b) => ({ slot: b.slot, type: b.type, comm: b.comm, contactor_control: b.contactor_control }));
    if (changed.length) out.batteries = changed;
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
  nav.append(navChip(LIVE_CATEGORY, t('category.live', 'Live controls')));
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
  if (field.min != null) input.min = String(field.min);
  if (field.max != null) input.max = String(field.max);
  input.value = current ?? '';
  return input;
}

const LIVE_REJECTED = t('ui.live_rejected', 'The device rejected this value.');

const isVisible = (key) => {
  if (!ownedBy(fieldFor(key))) return false;
  const rule = VISIBILITY[key];
  return !rule || rule(state);
};

// Consecutive schema rows sharing a section become one card; the firmware emits
// them grouped, so a section never reopens once it has closed.
function liveSections() {
  const sections = [];
  (data.schema ?? []).forEach((field) => {
    if (field.category !== LIVE_CATEGORY || !isVisible(field.key)) return;
    const open = sections[sections.length - 1];
    if (open && open.id === field.section) open.fields.push(field);
    else sections.push({ id: field.section, scope: field.scope, fields: [field] });
  });
  return sections;
}

// A battery-scoped section renders one card per dynamic.balancing entry — the
// firmware serves entries only for slots a POST would accept.
function liveCards() {
  return liveSections().flatMap((section) => {
    if (section.scope !== 'battery') return [{ ...section, key: section.id }];
    return (data.dynamic?.balancing ?? []).map((entry) => ({
      ...section,
      slot: entry.slot,
      values: entry,
      key: `${section.id}:${entry.slot}`,
    }));
  });
}

function liveErrorText(e) {
  let failure = null;
  try {
    failure = JSON.parse(e?.body ?? '');
  } catch {
    return e?.body || LIVE_REJECTED;
  }
  return tf(failure.error_key, failure.error, failure.error_arg);
}

// Live keys are applied on change, so the save bar must not see them as dirty.
function adoptLiveResult(result) {
  data = result;
  (data.schema ?? []).forEach((field) => {
    if (field.category !== LIVE_CATEGORY || field.scope === 'battery') return;
    state[field.key] = result.values?.[field.key];
    snapshot[field.key] = result.values?.[field.key];
  });
}

// Repaints only this card from the device's answer: the UI must reflect what
// the device applied, not what was typed.
async function onLiveChange(card, field, ctrl) {
  const repaintCard = () => {
    const existing = root.querySelector(`.settings-live-section[data-section="${card.key}"]`);
    const rebuilt = liveCards().find((c) => c.key === card.key);
    if (existing && rebuilt) existing.replaceWith(buildLiveSection(rebuilt));
  };
  let value;
  if (field.type === 'bool') {
    value = ctrl.checked;
  } else {
    // A cleared number input parses to 0; skip the write (and restore the device
    // value) so clearing a field to retype it never live-writes 0.
    value = Number(ctrl.value);
    if (ctrl.value === '' || Number.isNaN(value)) {
      repaintCard();
      return;
    }
  }
  ctrl.disabled = true;
  try {
    const body = card.slot === undefined
      ? { values: { [field.key]: value } }
      : { dynamic: { balancing: [{ slot: card.slot, [field.key]: value }] } };
    adoptLiveResult(await postJson('/api/settings', body));
    delete liveErrors[card.key];
  } catch (e) {
    // The change did not take; re-render restores the device's last-known
    // values and shows why it was refused.
    liveErrors[card.key] = liveErrorText(e);
  } finally {
    ctrl.disabled = false;
  }
  repaintCard();
}

function buildLiveSection(card) {
  const wrap = el('div', 'settings-live-section');
  wrap.dataset.section = card.key;
  const title = LIVE_SECTION_TITLES[card.id] ?? prettify(card.id);
  wrap.append(el('h3', null,
                 card.slot > 0 ? tf('ui.section_for_battery', '{} (battery {})', title, card.slot + 1) : title));
  if (liveErrors[card.key]) wrap.append(el('div', 'settings-error', liveErrors[card.key]));
  card.fields.forEach((field) => {
    const row = el('div', 'field');
    const current = card.values ? card.values[field.key] : state[field.key];
    const ctrl = liveControl(field, current);
    ctrl.addEventListener('change', () => onLiveChange(card, field, ctrl));
    row.append(el('label', null, labelFor(field)), ctrl);
    wrap.append(row);
  });
  return wrap;
}

function buildLivePanel() {
  const panel = el('div', 'settings-panel settings-live');
  panel.append(el('div', 'settings-live-note', t('ui.live_controls_note', 'These controls apply immediately — no reboot required.')));
  liveCards().forEach((card) => panel.append(buildLiveSection(card)));
  return panel;
}

function buildValues() {
  const values = {};
  (data.schema ?? []).forEach((field) => {
    if (field.category === LIVE_CATEGORY) return;
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
    if (field.category === LIVE_CATEGORY) continue;
    if (!ownedBy(field)) continue;
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
    if (!skin.contiguous && bad.category !== activeCategory) {
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
      const failure = await res.json();
      setSaveError(tf(failure.error_key, failure.error, failure.error_arg));
      return;
    }
    if (!res.ok) {
      setSaveError(SAVE_FAILED);
      return;
    }
    const result = await res.json();
    const rebuilt = RELOAD_ON_CHANGE.some((key) => data?.values?.[key] !== result.values?.[key]);
    data = result;
    seedState(result.values);
    seedDynamic(result.dynamic);
    snapshot = { ...state };
    setSaveError(null);
    renderShell();
    if (result.meta?.reboot_required === true && window.confirm(REBOOT_TO_APPLY)) {
      fetch('/reboot');
      return;
    }
    if (rebuilt) location.reload();
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
  const message = el('span', 'settings-savebar-msg', t('ui.unsaved_changes', 'Unsaved changes'));
  const save = el('button', 'btn', t('ui.save_and_apply', 'Save & apply'));
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
  const btn = el('button', 'btn btn-fault', t('ui.factory_reset', 'Factory reset'));
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

function panelFor(category) {
  return category === LIVE_CATEGORY ? buildLivePanel() : buildPanel(category);
}

function titledPanel(category, name) {
  const panel = panelFor(category);
  panel.prepend(el('h3', null, name));
  return panel;
}

function renderShell() {
  if (skin.contiguous) {
    const form = el('div', 'settings-form');
    [...CATEGORIES, [LIVE_CATEGORY, t('category.live', 'Live controls')]].forEach(([id, name]) => form.append(titledPanel(id, name)));
    root.replaceChildren(form, buildSaveBar(), buildDangerZone());
    return;
  }
  root.replaceChildren(buildNav(), panelFor(activeCategory), buildSaveBar(), buildDangerZone());
}

export async function mount(container) {
  root = container;
  try {
    data = await getJson('/api/settings');
  } catch {
    root.replaceChildren(el('div', 'settings-error', t('ui.settings_load_failed', 'Could not load settings. The device may be busy — try again.')));
    return;
  }
  seedState(data.values);
  seedDynamic(data.dynamic);
  snapshot = { ...state };
  setSaveError(null);
  liveErrors = {};
  activeCategory = CATEGORIES[0][0];
  renderShell();
}

// Settings loads once in mount, not from the /api/state poll, so a repaint from
// the poll must not disturb the form or the live panel's in-progress edits.
export function render() {}
