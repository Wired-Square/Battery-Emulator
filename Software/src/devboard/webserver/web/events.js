import { getJson, postJson } from '/app.js';

const REFRESH_MS = 5000;

const LEVEL_PILL = {
  ERROR: 'pill-fault',
  WARNING: 'pill-warn',
  UPDATE: 'pill-pending',
  INFO: 'pill-on',
  DEBUG: 'pill-off',
};

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

const MS_PER_SECOND = 1000;
const SECONDS_PER_MINUTE = 60;
const MINUTES_PER_HOUR = 60;
const HOURS_PER_DAY = 24;

function agoText(ms) {
  const s = Math.floor(ms / MS_PER_SECOND);
  if (s < SECONDS_PER_MINUTE) return `${s}s ago`;
  const m = Math.floor(s / SECONDS_PER_MINUTE);
  if (m < MINUTES_PER_HOUR) return `${m}m ago`;
  const h = Math.floor(m / MINUTES_PER_HOUR);
  if (h < HOURS_PER_DAY) return `${h}h ago`;
  return `${Math.floor(h / HOURS_PER_DAY)}d ago`;
}

let root = null;
let timer = null;

function paint(data) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', 'Events'));

  const bar = el('div', 'action-row');
  const refresh = el('button', 'btn', 'Refresh');
  refresh.type = 'button';
  refresh.addEventListener('click', () => reload().catch(() => window.alert('Could not refresh events.')));
  const clear = el('button', 'btn btn-fault', 'Clear all events');
  clear.type = 'button';
  clear.addEventListener('click', async () => {
    if (!window.confirm('Clear all events?')) return;
    try {
      await postJson('/api/events/clear', {});
    } catch {
      window.alert('Could not clear events.');
      return;
    }
    reload();
  });
  bar.append(refresh, clear);
  wrap.append(bar);

  if (!data.events.length) {
    wrap.append(el('div', 'muted', 'No events recorded.'));
    root.replaceChildren(wrap);
    return;
  }

  const table = el('table', 'data-table');
  const headRow = el('tr');
  ['Event', 'Severity', 'Last seen', 'Count', 'Data', 'Message'].forEach((h) => headRow.append(el('th', null, h)));
  const thead = el('thead');
  thead.append(headRow);
  table.append(thead);
  const body = el('tbody');
  data.events.forEach((ev) => {
    const tr = el('tr');
    tr.append(el('td', null, ev.type));
    const sev = el('td');
    sev.append(el('span', `pill ${LEVEL_PILL[ev.level] ?? 'pill-off'}`, ev.level));
    tr.append(sev);
    tr.append(el('td', null, agoText(ev.millis_ago)));
    tr.append(el('td', 'num', String(ev.count)));
    tr.append(el('td', 'num', String(ev.data)));
    tr.append(el('td', null, ev.message));
    body.append(tr);
  });
  table.append(body);
  wrap.append(table);
  root.replaceChildren(wrap);
}

async function reload() {
  paint(await getJson('/api/events'));
}

export async function mount(container) {
  root = container;
  await reload();
  timer = setInterval(() => reload().catch(() => {}), REFRESH_MS);
}

export function render() {}

export function unmount() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
}
