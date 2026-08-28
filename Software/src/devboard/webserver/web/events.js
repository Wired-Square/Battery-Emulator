import { getJson, postJson, t, tf } from '/app.js';

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
  if (s < SECONDS_PER_MINUTE) return tf('time.seconds_ago', '{}s ago', s);
  const m = Math.floor(s / SECONDS_PER_MINUTE);
  if (m < MINUTES_PER_HOUR) return tf('time.minutes_ago', '{}m ago', m);
  const h = Math.floor(m / MINUTES_PER_HOUR);
  if (h < HOURS_PER_DAY) return tf('time.hours_ago', '{}h ago', h);
  return tf('time.days_ago', '{}d ago', Math.floor(h / HOURS_PER_DAY));
}

let root = null;
let timer = null;

function paint(data) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', t('ui.events', 'Events')));

  const bar = el('div', 'action-row');
  const refresh = el('button', 'btn', t('ui.refresh', 'Refresh'));
  refresh.type = 'button';
  refresh.addEventListener('click',
                            () => reload().catch(() => window.alert(t('ui.events_refresh_failed',
                                                                   'Could not refresh events.'))));
  const clear = el('button', 'btn btn-fault', t('ui.clear_all_events', 'Clear all events'));
  clear.type = 'button';
  clear.addEventListener('click', async () => {
    if (!window.confirm(t('ui.confirm_clear_events', 'Clear all events?'))) return;
    try {
      await postJson('/api/events/clear', {});
    } catch {
      window.alert(t('ui.events_clear_failed', 'Could not clear events.'));
      return;
    }
    reload();
  });
  bar.append(refresh, clear);
  wrap.append(bar);

  if (!data.events.length) {
    wrap.append(el('div', 'muted', t('ui.no_events', 'No events recorded.')));
    root.replaceChildren(wrap);
    return;
  }

  const table = el('table', 'data-table');
  const headRow = el('tr');
  [t('ui.col_event', 'Event'), t('ui.col_severity', 'Severity'), t('ui.col_last_seen', 'Last seen'),
   t('ui.col_count', 'Count'), t('ui.col_data', 'Data'), t('ui.col_message', 'Message')]
    .forEach((h) => headRow.append(el('th', null, h)));
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
    tr.append(el('td', null, t(`event.${ev.type}`, ev.message)));
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
