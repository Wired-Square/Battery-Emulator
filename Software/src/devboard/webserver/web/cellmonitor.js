import { getJson, t, tf } from '/app.js';

const REFRESH_MS = 2000;
const BAR_TRACK_PX = 200;
const BAR_FLOOR_PX = 20;
const RANGE_PAD_MV = 20;

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

let root = null;
let timer = null;
// Last two readings per series, so a counter series can show what each cell
// gained between them. Keyed by slot and series id.
const seriesReadings = new Map();

function priorReading(slot, series) {
  const key = `${slot}:${series.id}`;
  const seen = seriesReadings.get(key);
  if (seen && seen.revision === series.revision) return seen.prior;
  seriesReadings.set(key, { revision: series.revision, values: series.values, prior: seen?.values ?? null });
  return seen?.values ?? null;
}

function formatReading(value, series) {
  if (value === null || value === undefined) return '—';
  const text = Number(value).toFixed(series.decimals ?? 0);
  return series.unit ? `${text} ${series.unit}` : text;
}

const SERIES_STATE_TEXT = {
  unread: () => t('ui.series_unread', 'Not read yet.'),
  pending: () => t('ui.series_pending', 'Queued — the reading starts once the battery is idle.'),
  reading: (s) => tf('ui.series_reading', 'Reading: {} of {} cells so far.', s.read, s.expected),
  complete: (s) => tf('ui.series_complete', 'Read {} cells.', s.read),
  partial: (s) => tf('ui.series_partial', 'Read {} of {} cells.', s.read, s.expected),
  failed: () => t('ui.series_failed', 'The battery answered none of the requests.'),
};

function seriesSection(battery, series) {
  const wrap = el('div', 'cell-series');
  wrap.append(el('h4', null, t(`cellseries.${series.id}`, series.label)));
  const state = SERIES_STATE_TEXT[series.state];
  if (state) wrap.append(el('div', 'muted', state(series)));
  const prior = series.kind === 'counter' ? priorReading(battery.slot ?? 0, series) : null;
  const grid = el('div', 'cell-grid');
  (series.values ?? []).forEach((value, i) => {
    const cell = el('div', 'cell');
    const gained = prior && value != null && prior[i] != null && value > prior[i];
    if (gained) cell.classList.add('cell-bal');
    cell.append(el('div', 'cell-index', String(i + 1)));
    cell.append(el('div', 'cell-value', formatReading(value, series)));
    grid.append(cell);
  });
  wrap.append(grid);
  return wrap;
}

function batterySection(battery, index, multi) {
  const section = el('div', 'card');
  if (multi) section.append(el('h3', null, tf('ui.battery_n', 'Battery {}', (battery.slot ?? index) + 1)));

  if (!battery.cells.length) {
    section.append(el('div', 'muted', t('ui.no_cell_voltages', 'No cell voltages read yet.')));
    (battery.series ?? []).forEach((series) => section.append(seriesSection(battery, series)));
    return section;
  }

  const min = Math.min(...battery.cells);
  const max = Math.max(...battery.cells);
  section.append(el('div', 'muted', tf('ui.cell_summary', 'Max {} mV · Min {} mV · Deviation {} mV', max, min, max - min)));
  if (battery.balancing_active || battery.balancing_pending) {
    section.append(el('span', 'pill pill-pending', battery.balancing_pending ? 'Pending' : 'Balancing'));
  }

  const grid = el('div', 'cell-grid');
  battery.cells.forEach((mv, i) => {
    const cell = el('div', 'cell');
    if (mv === min) cell.classList.add('cell-min');
    if (mv === max) cell.classList.add('cell-max');
    if (battery.balancing[i]) cell.classList.add('cell-bal');
    cell.append(el('div', 'cell-index', String(i + 1)));
    cell.append(el('div', 'cell-mv', String(mv)));
    grid.append(cell);
  });
  section.append(grid);

  const lo = min - RANGE_PAD_MV;
  const span = Math.max(max + RANGE_PAD_MV - lo, 1);
  const bars = el('div', 'cell-bars');
  battery.cells.forEach((mv, i) => {
    const bar = el('div', 'cell-bar');
    bar.style.height = `${BAR_FLOOR_PX + ((mv - lo) / span) * (BAR_TRACK_PX - BAR_FLOOR_PX)}px`;
    if (battery.balancing[i]) bar.classList.add('cell-bar-bal');
    bar.title = tf('ui.cell_tooltip', 'Cell {}: {} mV', i + 1, mv);
    bars.append(bar);
  });
  section.append(bars);
  (battery.series ?? []).forEach((series) => section.append(seriesSection(battery, series)));
  return section;
}

function paint(data) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', t('ui.cells', 'Cells')));
  if (!data.batteries.length) {
    wrap.append(el('div', 'muted', t('ui.no_battery_configured', 'No battery configured.')));
  } else {
    const multi = data.batteries.length > 1;
    data.batteries.forEach((b, i) => wrap.append(batterySection(b, i, multi)));
  }
  root.replaceChildren(wrap);
}

async function reload() {
  paint(await getJson('/api/cellmonitor'));
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
