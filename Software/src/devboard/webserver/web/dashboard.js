import { postJson, repaint } from '/app.js';

const EMPTY = '—';

const CONFIRM_PAUSE =
  'Are you sure you want to pause charging and discharging? This will set the maximum charge and discharge '
  + 'values to zero, preventing any further power flow.';
const CONFIRM_OPEN_CONTACTORS = 'This action will attempt to open contactors on the battery. Are you sure?';
const CONFIRM_CLOSE_CONTACTORS = 'This action will attempt to close contactors and enable power transfer. Are you sure?';
const CONFIRM_REBOOT =
  'Are you sure you want to reboot the emulator? NOTE: If emulator is handling contactors, they will open '
  + 'during reboot!';

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
  c.append(el('h2', null, title));
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

export function render(state) {
  if (!root) return;
  const vitals = el('div', 'vitals');
  const b = state.battery ?? {};
  vitals.append(tile('SOC', b.soc?.toFixed(1), '%'),
                tile('Voltage', b.voltage?.toFixed(1), 'V'),
                tile('Current', b.current?.toFixed(1), 'A'),
                tile('Power', b.power, 'W'));

  const cards = el('div', 'cards');
  // First card: reaching the emergency stop must never mean scrolling past vitals.
  cards.append(actionsCard(state.system ?? {}));
  cards.append(card('System', [
    row('Status', state.system?.status),
    row('Uptime', state.system?.uptime),
    row('Free heap', present(state.system?.free_heap) ? `${state.system.free_heap} B` : null),
  ]));
  const wifi = state.wifi ?? {};
  cards.append(card('Wi-Fi network', [
    row('SSID', wifi.ssid),
    row('Status', wifi.connected ? 'Connected' : 'Not connected'),
    row('IP address', wifi.ip),
    row('Hostname', wifi.hostname),
    row('MAC address', wifi.mac),
    row('Signal', present(wifi.rssi) ? `${wifi.rssi} dBm (ch ${wifi.channel})` : null),
  ]));
  if (wifi.ap_active) {
    cards.append(card('Wi-Fi access point', [
      row('SSID', wifi.ap_ssid),
      row('IP address', wifi.ap_ip),
    ]));
  }
  (state.batteries ?? []).forEach((pack) => {
    cards.append(card(`Battery ${pack.slot + 1}`, [
      row('Protocol', pack.name),
      row('SOC', present(pack.soc) ? `${pack.soc.toFixed(1)} %` : null),
      row('Real SOC', present(pack.soc_real) ? `${pack.soc_real.toFixed(1)} %` : null),
      row('Health', present(pack.soh) ? `${pack.soh.toFixed(1)} %` : null),
      row('Voltage', present(pack.voltage) ? `${pack.voltage.toFixed(1)} V` : null),
      row('Current', present(pack.current) ? `${pack.current.toFixed(1)} A` : null),
      row('Power', present(pack.power) ? `${pack.power} W` : null),
      row('Cell min', present(pack.cell_min_mV) ? `${pack.cell_min_mV} mV` : null),
      row('Cell max', present(pack.cell_max_mV) ? `${pack.cell_max_mV} mV` : null),
    ]));
  });
  if (state.inverter) {
    cards.append(card('Inverter', [row('Protocol', state.inverter.name)]));
  }
  if (state.charger) {
    const chg = state.charger;
    cards.append(card('Charger', [
      row('Type', chg.name),
      row('Status', chg.alive ? 'Connected' : 'Not responding'),
      row('HV output', present(chg.hv_v) ? `${chg.hv_v.toFixed(1)} V · ${chg.hv_a.toFixed(1)} A` : null),
      row('AC input', present(chg.ac_v) ? `${chg.ac_v.toFixed(1)} V · ${chg.ac_a.toFixed(1)} A` : null),
      row('LV output', present(chg.lv_v) ? `${chg.lv_v.toFixed(1)} V · ${chg.lv_a.toFixed(1)} A` : null),
      row('HV charging', chg.hv_enabled ? 'Enabled' : 'Disabled'),
      row('Aux 12V', chg.aux12v_enabled ? 'Enabled' : 'Disabled'),
    ]));
  }
  if (state.events) {
    cards.append(card('Events', [
      row('Active', state.events.active),
      row('Latest', state.events.latest),
    ]));
  }
  if (state.load_switch) cards.append(loadSwitchCard(state.load_switch));

  root.replaceChildren(vitals, cards);
}
