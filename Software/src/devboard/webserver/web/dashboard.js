import { postJson, repaint, skinName, deviceCapabilities, t, tf, uptimeText } from '/app.js';

const EMPTY = '—';
const WH_PER_KWH = 1000;
const W_PER_KW = 1000;

const CONFIRM_PAUSE =
  'Are you sure you want to pause charging and discharging? This will set the maximum charge and discharge '
  + 'values to zero, preventing any further power flow.';
const CONFIRM_OPEN_CONTACTORS = 'This action will attempt to open contactors on the battery. Are you sure?';
const CONFIRM_CLOSE_CONTACTORS = 'This action will attempt to close contactors and enable power transfer. Are you sure?';
const CONFIRM_REBOOT =
  'Are you sure you want to reboot the emulator? NOTE: If emulator is handling contactors, they will open '
  + 'during reboot!';

const SKINS = {
  modern: { vitals: true, model: (state) => modelModern(state) },
  legacy: { vitals: false, model: (state) => modelLegacy(state) },
};

const skin = SKINS[skinName] ?? SKINS.modern;

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

const present = (v) => v !== undefined && v !== null;

function tile(label, value, unit) {
  const t = el('div', 'tile');
  t.append(el('div', 'tile-label', label));
  const v = el('div', 'tile-value', present(value) ? value : EMPTY);
  if (present(value) && unit) v.append(el('span', 'tile-unit', ` ${unit}`));
  t.append(v);
  return t;
}

function row(label, value) {
  const r = el('div', 'row');
  r.append(el('span', null, label), el('span', 'row-value', present(value) ? value : EMPTY));
  return r;
}

function card(title, rows) {
  const c = el('div', 'card');
  if (title !== undefined) c.append(el('h2', null, title));
  rows.forEach((r) => c.append(r));
  return c;
}

// A channel is honest about the gap between what was asked and what the
// device reached: the tick applies the request, the next poll confirms it.
function channelPill(ch) {
  if (ch.fault) return el('span', 'pill pill-fault', t('ui.fault', 'Fault'));
  if (ch.pending) {
    return el('span', 'pill pill-pending',
              ch.pending_on ? t('ui.turning_on', 'Turning on…') : t('ui.turning_off', 'Turning off…'));
  }
  return el('span', `pill ${ch.on ? 'pill-on' : 'pill-off'}`,
            ch.on ? t('ui.on', 'On') : t('ui.off', 'Off'));
}

function loadSwitchCard(ls) {
  const c = el('div', 'card');
  c.append(el('h2', null, t('ui.load_switch', 'Load switch')));
  if (!ls.device_ok) {
    c.append(el('div', 'muted', t('ui.device_not_responding', 'Device not responding')));
    return c;
  }
  ls.channels.forEach((ch, i) => {
    const line = el('div', 'channel');
    const meta = el('div', 'channel-meta');
    meta.append(el('strong', null, `CH${i} · ${ch.role}`),
                el('span', 'muted', `${ch.current_mA} mA`));
    const actions = el('div', 'channel-actions');
    actions.append(channelPill(ch));
    if (ch.manual) {
      const btn = el('button', 'btn', ch.on ? t('ui.turn_off', 'Turn off') : t('ui.turn_on', 'Turn on'));
      btn.type = 'button';
      btn.addEventListener('click', async () => {
        btn.disabled = true;
        try {
          repaint(await postJson('/api/loadswitch', { channel: i, on: !ch.on }));
        } finally {
          btn.disabled = false;
        }
      });
      actions.append(btn);
    }
    line.append(meta, actions);
    c.append(line);
  });
  return c;
}

function actionButton(label, cls, confirmText, run) {
  const btn = el('button', cls, label);
  btn.type = 'button';
  btn.addEventListener('click', async () => {
    if (confirmText && !window.confirm(confirmText)) return;
    btn.disabled = true;
    try {
      await run();
    } finally {
      btn.disabled = false;
    }
  });
  return btn;
}

// equipment_stop true is the stopped state: contactors open, no power transfer.
function actionsCard(sys) {
  const c = el('div', 'card');
  c.append(el('h2', null, t('ui.actions', 'Actions')));

  const stopped = sys.equipment_stop === true;
  const contactors = el('div', 'actions');
  contactors.append(
    actionButton(stopped ? t('ui.close_contactors', 'Close Contactors')
                         : t('ui.open_contactors', 'Open Contactors'),
                 `btn btn-critical ${stopped ? 'btn-ok' : 'btn-fault'}`,
                 stopped ? t('ui.confirm_close_contactors', CONFIRM_CLOSE_CONTACTORS)
                         : t('ui.confirm_open_contactors', CONFIRM_OPEN_CONTACTORS),
                 async () => repaint(await postJson('/api/equipmentstop', { on: !stopped }))),
    el('div', 'muted', stopped ? t('ui.equipment_stop_active', 'Equipment stop active — contactors open.')
                               : t('ui.power_transfer_enabled', 'Power transfer enabled.')),
  );
  c.append(contactors);

  const paused = sys.paused === true;
  const rest = el('div', 'action-row');
  rest.append(
    actionButton(paused ? t('ui.resume_charge', 'Resume charge/discharge')
                        : t('ui.pause_charge', 'Pause charge/discharge'), 'btn',
                 paused ? null : t('ui.confirm_pause', CONFIRM_PAUSE),
                 async () => repaint(await postJson('/api/pause', { on: !paused }))),
    actionButton(t('ui.reboot_emulator', 'Reboot Emulator'), 'btn',
                 t('ui.confirm_reboot', CONFIRM_REBOOT), () => fetch('/reboot')),
  );
  if (sys.auth === true) {
    const logout = el('a', 'btn', t('ui.logout', 'Logout'));
    logout.href = '/logout';
    rest.append(logout);
  }
  c.append(rest);
  return c;
}

let root = null;

export function mount(container) {
  root = container;
}

const ROW = {
  identity: () => {
    const caps = deviceCapabilities();
    return [{ key: 'row.software', label: 'Software', value: caps.firmware },
            { key: 'row.hardware', label: 'Hardware', value: caps.hardware }];
  },
  system: (sys) => [
    { key: 'row.uptime', label: 'Uptime', value: uptimeText(sys.uptime_s) ?? sys.uptime },
    { key: 'row.free_heap', label: 'Free heap', value: present(sys.free_heap) ? `${sys.free_heap} B` : null },
  ],
  wifi: (wifi) => [
    { key: 'row.ssid', label: 'SSID', value: wifi.ssid },
    { key: 'row.wifi_status', label: 'Status',
      value: wifi.connected ? t('ui.connected', 'Connected') : t('ui.not_connected', 'Not connected') },
    { key: 'row.ip', label: 'IP address', value: wifi.ip },
    { key: 'row.hostname', label: 'Hostname', value: wifi.hostname },
    { key: 'row.mac', label: 'MAC address', value: wifi.mac },
    { key: 'row.signal', label: 'Signal',
      value: present(wifi.rssi) ? `${wifi.rssi} dBm (ch ${wifi.channel})` : null },
  ],
  accessPoint: (wifi) => (wifi.ap_active
    ? [{ key: 'row.ap_ssid', label: 'Access point SSID', value: wifi.ap_ssid },
       { key: 'row.ap_ip', label: 'Access point IP', value: wifi.ap_ip }]
    : []),
  power: (sys) => [
    { key: 'row.power_status', label: 'Power status', value: sys.status },
    { key: 'row.contactors', label: 'Contactors',
      value: sys.equipment_stop === true ? t('ui.contactors_open_estop', 'Open (equipment stop)')
                                         : t('ui.contactors_closed', 'Closed') },
  ],
  packProtocol: (pack) => ({ key: 'row.battery_protocol', label: 'Battery {} protocol',
                             arg: pack.slot + 1, value: pack.name }),
  packSummary: (pack) => [
    { key: 'row.soc', label: 'SOC', value: pct(pack.soc) },
    { key: 'row.soc_real', label: 'Real SOC', value: pct(pack.soc_real) },
    { key: 'row.soh', label: 'Health', value: pct(pack.soh) },
    { key: 'row.voltage', label: 'Voltage', value: unit(pack.voltage, 'V', 1) },
    { key: 'row.current', label: 'Current', value: unit(pack.current, 'A', 1) },
    { key: 'row.power', label: 'Power', value: present(pack.power) ? `${pack.power} W` : null },
    { key: 'row.cell_min', label: 'Cell min', value: present(pack.cell_min_mV) ? `${pack.cell_min_mV} mV` : null },
    { key: 'row.cell_max', label: 'Cell max', value: present(pack.cell_max_mV) ? `${pack.cell_max_mV} mV` : null },
  ],
  packDetail: (pack) => [
    { key: 'row.total_capacity', label: 'Total capacity', value: kwh(pack.total_wh) },
    { key: 'row.remaining_capacity', label: 'Remaining capacity', value: kwh(pack.remaining_wh) },
    { key: 'row.max_discharge_power', label: 'Max discharge power', value: kw(pack.max_discharge_w) },
    { key: 'row.max_charge_power', label: 'Max charge power', value: kw(pack.max_charge_w) },
    { key: 'row.max_discharge_current', label: 'Max discharge current',
      value: unit(pack.max_discharge_a, 'A', 1) },
    { key: 'row.max_charge_current', label: 'Max charge current', value: unit(pack.max_charge_a, 'A', 1) },
    { key: 'row.cell_delta', label: 'Cell delta', value: cellDelta(pack) },
    { key: 'row.temp_range', label: 'Temperature min/max', value: tempRange(pack) },
  ],
  charger: (chg) => [
    { key: 'row.charger_type', label: 'Type', value: chg.name },
    { key: 'row.charger_status', label: 'Status',
      value: chg.alive ? t('ui.connected', 'Connected') : t('ui.not_responding', 'Not responding') },
    { key: 'row.hv_output', label: 'HV output', value: pair(chg.hv_v, 'V', chg.hv_a, 'A') },
    { key: 'row.ac_input', label: 'AC input', value: pair(chg.ac_v, 'V', chg.ac_a, 'A') },
    { key: 'row.lv_output', label: 'LV output', value: pair(chg.lv_v, 'V', chg.lv_a, 'A') },
    { key: 'row.hv_charging', label: 'HV charging',
      value: chg.hv_enabled ? t('ui.enabled', 'Enabled') : t('ui.disabled', 'Disabled') },
    { key: 'row.aux12v', label: 'Aux 12V',
      value: chg.aux12v_enabled ? t('ui.enabled', 'Enabled') : t('ui.disabled', 'Disabled') },
  ],
  events: (ev) => [
    { key: 'row.events_active', label: 'Active', value: ev.active },
    { key: 'row.events_latest', label: 'Latest',
      value: present(ev.latest) ? t(`event.${ev.latest_type}`, ev.latest) : null },
  ],
};

const pct = (v) => (present(v) ? `${v.toFixed(1)} %` : null);
const unit = (v, u, dp) => (present(v) ? `${v.toFixed(dp)} ${u}` : null);
const kwh = (wh) => (present(wh) ? `${(wh / WH_PER_KWH).toFixed(1)} kWh` : null);
const kw = (w) => (present(w) ? `${(w / W_PER_KW).toFixed(1)} kW` : null);
const pair = (a, ua, b, ub) => (present(a) ? `${a.toFixed(1)} ${ua} · ${b.toFixed(1)} ${ub}` : null);
const cellDelta = (pack) => (present(pack.cell_max_mV) && present(pack.cell_min_mV)
  ? `${pack.cell_max_mV - pack.cell_min_mV} mV` : null);
const tempRange = (pack) => (present(pack.temp_min_c) && present(pack.temp_max_c)
  ? `${pack.temp_min_c.toFixed(1)} °C / ${pack.temp_max_c.toFixed(1)} °C` : null);

function modelModern(state) {
  const sys = state.system ?? {};
  const wifi = state.wifi ?? {};
  const blocks = [
    { id: 'actions', section: 'actions', kind: 'component', build: () => actionsCard(sys) },
    { id: 'system', section: 'system', title: t('ui.system', 'System'),
      kind: 'rows', rows: [{ key: 'row.system_status', label: 'Status', value: sys.status }, ...ROW.system(sys)] },
    { id: 'wifi', section: 'wifi', title: t('ui.wifi_network', 'Wi-Fi network'), kind: 'rows', rows: ROW.wifi(wifi) },
  ];
  if (wifi.ap_active) {
    blocks.push({ id: 'wifiap', section: 'wifiap', title: t('ui.wifi_access_point', 'Wi-Fi access point'), kind: 'rows',
      rows: [{ key: 'row.ssid', label: 'SSID', value: wifi.ap_ssid },
             { key: 'row.ip', label: 'IP address', value: wifi.ap_ip }] });
  }
  (state.batteries ?? []).forEach((pack) => {
    blocks.push({ id: `battery${pack.slot}`, section: 'battery', title: tf('ui.battery_n', 'Battery {}', pack.slot + 1),
      kind: 'rows', status: sys.emulator_status,
      rows: [{ key: 'row.protocol', label: 'Protocol', value: pack.name },
             ...ROW.packSummary(pack), ...ROW.packDetail(pack)] });
  });
  if (state.inverter) {
    blocks.push({ id: 'inverter', section: 'inverter', title: t('ui.inverter', 'Inverter'), kind: 'rows',
      rows: [{ key: 'row.protocol', label: 'Protocol', value: state.inverter.name }] });
  }
  if (state.charger) {
    blocks.push({ id: 'charger', section: 'charger', title: t('ui.charger', 'Charger'), kind: 'rows',
      rows: ROW.charger(state.charger) });
  }
  if (state.events) {
    blocks.push({ id: 'events', section: 'events', title: t('ui.events', 'Events'), kind: 'rows',
      rows: ROW.events(state.events) });
  }
  if (state.load_switch) {
    blocks.push({ id: 'loadswitch', section: 'loadswitch', kind: 'component',
      build: () => loadSwitchCard(state.load_switch) });
  }
  return blocks;
}

function modelLegacy(state) {
  const sys = state.system ?? {};
  const wifi = state.wifi ?? {};
  const packs = state.batteries ?? [];
  const blocks = [
    { id: 'identity', section: 'system', kind: 'rows',
      rows: [...ROW.identity(), ...ROW.system(sys), ...ROW.wifi(wifi), ...ROW.accessPoint(wifi)] },
    { id: 'protocols', section: 'inverter', kind: 'rows',
      rows: [{ key: 'row.inverter_protocol', label: 'Inverter protocol', value: state.inverter?.name },
             ...packs.map((pack) => ROW.packProtocol(pack))] },
  ];
  packs.forEach((pack) => {
    blocks.push({ id: `battery${pack.slot}`, section: 'battery', kind: 'rows', status: sys.emulator_status,
      rows: [...ROW.packSummary(pack), ...ROW.packDetail(pack)] });
  });
  blocks.push({ id: 'power', section: 'power', kind: 'rows', rows: ROW.power(sys) });
  if (state.charger) {
    blocks.push({ id: 'charger', section: 'charger', kind: 'rows', rows: ROW.charger(state.charger) });
  }
  if (state.events) {
    blocks.push({ id: 'events', section: 'events', kind: 'rows', rows: ROW.events(state.events) });
  }
  if (state.load_switch) {
    blocks.push({ id: 'loadswitch', section: 'loadswitch', kind: 'component',
      build: () => loadSwitchCard(state.load_switch) });
  }
  blocks.push({ id: 'actions', section: 'actions', kind: 'component', build: () => actionsCard(sys) });
  return blocks;
}

function vitalsRow(state) {
  const b = state.battery ?? {};
  const vitals = el('div', 'vitals');
  vitals.append(tile(t('row.soc', 'SOC'), b.soc?.toFixed(1), '%'),
                tile(t('row.voltage', 'Voltage'), b.voltage?.toFixed(1), 'V'),
                tile(t('row.current', 'Current'), b.current?.toFixed(1), 'A'),
                tile(t('row.power', 'Power'), b.power, 'W'));
  return vitals;
}

function paint(blocks, active, state) {
  const cards = el('div', 'cards');
  blocks.forEach((b) => {
    const node = b.kind === 'component'
      ? b.build()
      : card(b.title, b.rows.map((r) => row(tf(r.key, r.label, r.arg), r.value)));
    node.dataset.section = b.section;
    if (b.status) node.dataset.status = b.status;
    cards.append(node);
  });
  if (active.vitals) root.replaceChildren(vitalsRow(state), cards);
  else root.replaceChildren(cards);
}

export function render(state) {
  if (!root) return;
  paint(skin.model(state), skin, state);
}
