import { postJson, repaint, skinName, deviceCapabilities } from '/app.js';

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
  if (ch.fault) return el('span', 'pill pill-fault', 'Fault');
  if (ch.pending) return el('span', 'pill pill-pending', ch.pending_on ? 'Turning on…' : 'Turning off…');
  return el('span', `pill ${ch.on ? 'pill-on' : 'pill-off'}`, ch.on ? 'On' : 'Off');
}

function loadSwitchCard(ls) {
  const c = el('div', 'card');
  c.append(el('h2', null, 'Load switch'));
  if (!ls.device_ok) {
    c.append(el('div', 'muted', 'Device not responding'));
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
      const btn = el('button', 'btn', ch.on ? 'Turn off' : 'Turn on');
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
  c.append(el('h2', null, 'Actions'));

  const stopped = sys.equipment_stop === true;
  const contactors = el('div', 'actions');
  contactors.append(
    actionButton(stopped ? 'Close Contactors' : 'Open Contactors',
                 `btn btn-critical ${stopped ? 'btn-ok' : 'btn-fault'}`,
                 stopped ? CONFIRM_CLOSE_CONTACTORS : CONFIRM_OPEN_CONTACTORS,
                 async () => repaint(await postJson('/api/equipmentstop', { on: !stopped }))),
    el('div', 'muted', stopped ? 'Equipment stop active — contactors open.' : 'Power transfer enabled.'),
  );
  c.append(contactors);

  const paused = sys.paused === true;
  const rest = el('div', 'action-row');
  rest.append(
    actionButton(paused ? 'Resume charge/discharge' : 'Pause charge/discharge', 'btn',
                 paused ? null : CONFIRM_PAUSE,
                 async () => repaint(await postJson('/api/pause', { on: !paused }))),
    actionButton('Reboot Emulator', 'btn', CONFIRM_REBOOT, () => fetch('/reboot')),
  );
  if (sys.auth === true) {
    const logout = el('a', 'btn', 'Logout');
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
    return [{ label: 'Software', value: caps.firmware }, { label: 'Hardware', value: caps.hardware }];
  },
  system: (sys) => [
    { label: 'Uptime', value: sys.uptime },
    { label: 'Free heap', value: present(sys.free_heap) ? `${sys.free_heap} B` : null },
  ],
  wifi: (wifi) => [
    { label: 'SSID', value: wifi.ssid },
    { label: 'Status', value: wifi.connected ? 'Connected' : 'Not connected' },
    { label: 'IP address', value: wifi.ip },
    { label: 'Hostname', value: wifi.hostname },
    { label: 'MAC address', value: wifi.mac },
    { label: 'Signal', value: present(wifi.rssi) ? `${wifi.rssi} dBm (ch ${wifi.channel})` : null },
  ],
  accessPoint: (wifi) => (wifi.ap_active
    ? [{ label: 'Access point SSID', value: wifi.ap_ssid }, { label: 'Access point IP', value: wifi.ap_ip }]
    : []),
  power: (sys) => [
    { label: 'Power status', value: sys.status },
    { label: 'Contactors', value: sys.equipment_stop === true ? 'Open (equipment stop)' : 'Closed' },
  ],
  packProtocol: (pack) => ({ label: `Battery ${pack.slot + 1} protocol`, value: pack.name }),
  packSummary: (pack) => [
    { label: 'SOC', value: pct(pack.soc) },
    { label: 'Real SOC', value: pct(pack.soc_real) },
    { label: 'Health', value: pct(pack.soh) },
    { label: 'Voltage', value: unit(pack.voltage, 'V', 1) },
    { label: 'Current', value: unit(pack.current, 'A', 1) },
    { label: 'Power', value: present(pack.power) ? `${pack.power} W` : null },
    { label: 'Cell min', value: present(pack.cell_min_mV) ? `${pack.cell_min_mV} mV` : null },
    { label: 'Cell max', value: present(pack.cell_max_mV) ? `${pack.cell_max_mV} mV` : null },
  ],
  packDetail: (pack) => [
    { label: 'Total capacity', value: kwh(pack.total_wh) },
    { label: 'Remaining capacity', value: kwh(pack.remaining_wh) },
    { label: 'Max discharge power', value: kw(pack.max_discharge_w) },
    { label: 'Max charge power', value: kw(pack.max_charge_w) },
    { label: 'Max discharge current', value: unit(pack.max_discharge_a, 'A', 1) },
    { label: 'Max charge current', value: unit(pack.max_charge_a, 'A', 1) },
    { label: 'Cell delta', value: cellDelta(pack) },
    { label: 'Temperature min/max', value: tempRange(pack) },
  ],
  charger: (chg) => [
    { label: 'Type', value: chg.name },
    { label: 'Status', value: chg.alive ? 'Connected' : 'Not responding' },
    { label: 'HV output', value: pair(chg.hv_v, 'V', chg.hv_a, 'A') },
    { label: 'AC input', value: pair(chg.ac_v, 'V', chg.ac_a, 'A') },
    { label: 'LV output', value: pair(chg.lv_v, 'V', chg.lv_a, 'A') },
    { label: 'HV charging', value: chg.hv_enabled ? 'Enabled' : 'Disabled' },
    { label: 'Aux 12V', value: chg.aux12v_enabled ? 'Enabled' : 'Disabled' },
  ],
  events: (ev) => [
    { label: 'Active', value: ev.active },
    { label: 'Latest', value: ev.latest },
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
    { id: 'system', section: 'system', title: 'System',
      kind: 'rows', rows: [{ label: 'Status', value: sys.status }, ...ROW.system(sys)] },
    { id: 'wifi', section: 'wifi', title: 'Wi-Fi network', kind: 'rows', rows: ROW.wifi(wifi) },
  ];
  if (wifi.ap_active) {
    blocks.push({ id: 'wifiap', section: 'wifiap', title: 'Wi-Fi access point', kind: 'rows',
      rows: [{ label: 'SSID', value: wifi.ap_ssid }, { label: 'IP address', value: wifi.ap_ip }] });
  }
  (state.batteries ?? []).forEach((pack) => {
    blocks.push({ id: `battery${pack.slot}`, section: 'battery', title: `Battery ${pack.slot + 1}`,
      kind: 'rows', status: sys.emulator_status,
      rows: [{ label: 'Protocol', value: pack.name }, ...ROW.packSummary(pack), ...ROW.packDetail(pack)] });
  });
  if (state.inverter) {
    blocks.push({ id: 'inverter', section: 'inverter', title: 'Inverter', kind: 'rows',
      rows: [{ label: 'Protocol', value: state.inverter.name }] });
  }
  if (state.charger) {
    blocks.push({ id: 'charger', section: 'charger', title: 'Charger', kind: 'rows',
      rows: ROW.charger(state.charger) });
  }
  if (state.events) {
    blocks.push({ id: 'events', section: 'events', title: 'Events', kind: 'rows', rows: ROW.events(state.events) });
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
      rows: [{ label: 'Inverter protocol', value: state.inverter?.name },
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
  vitals.append(tile('SOC', b.soc?.toFixed(1), '%'),
                tile('Voltage', b.voltage?.toFixed(1), 'V'),
                tile('Current', b.current?.toFixed(1), 'A'),
                tile('Power', b.power, 'W'));
  return vitals;
}

function paint(blocks, active, state) {
  const cards = el('div', 'cards');
  blocks.forEach((b) => {
    const node = b.kind === 'component' ? b.build() : card(b.title, b.rows.map((r) => row(r.label, r.value)));
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
