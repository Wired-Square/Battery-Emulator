import { getJson } from '/app.js';

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

function batterySection(battery, index, multi) {
  const section = el('div', 'card');
  if (multi) section.append(el('h3', null, `Battery ${index + 1}`));

  if (!battery.cells.length) {
    section.append(el('div', 'muted', 'No cell voltages read yet.'));
    return section;
  }

  const min = Math.min(...battery.cells);
  const max = Math.max(...battery.cells);
  section.append(el('div', 'muted', `Max ${max} mV · Min ${min} mV · Deviation ${max - min} mV`));
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
    bar.title = `Cell ${i + 1}: ${mv} mV`;
    bars.append(bar);
  });
  section.append(bars);
  return section;
}

function paint(data) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', 'Cells'));
  if (!data.batteries.length) {
    wrap.append(el('div', 'muted', 'No battery configured.'));
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
