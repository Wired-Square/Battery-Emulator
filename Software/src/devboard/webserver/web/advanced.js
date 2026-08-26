import { getJson, postJson } from '/app.js';

const REFRESH_MS = 2000;

// Description catalogues live in the upstream repository's data folder, not on the device.
const CATALOGUE_BASE_URL = 'https://raw.githubusercontent.com/dalathegreat/Battery-Emulator/main/web_data/dtc/';
const CATALOGUE_CACHE_PREFIX = 'catalogue:';

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

let root = null;
let timer = null;

// Resolved catalogues, by filename. A pending fetch is stored as its promise so the
// repaint interval cannot start a second request for the same file.
const catalogues = new Map();

// Entries may carry a numeric "code", a "dtc" string, or both; key on whichever is present.
function indexCatalogue(entries) {
  const map = new Map();
  entries.forEach((e) => {
    if (e.code !== undefined && e.code !== null) map.set(String(e.code), e);
    if (e.dtc) map.set(String(e.dtc), e);
  });
  return map;
}

function loadCatalogue(filename) {
  if (catalogues.has(filename)) return catalogues.get(filename);

  const cacheKey = CATALOGUE_CACHE_PREFIX + filename;
  const promise = (async () => {
    try {
      const cached = window.localStorage.getItem(cacheKey);
      if (cached) return indexCatalogue(JSON.parse(cached));
    } catch {
      // Unreadable or evicted cache is not an error; fall through to the network.
    }
    const res = await fetch(CATALOGUE_BASE_URL + filename);
    if (!res.ok) throw new Error(`${res.status}`);
    const entries = await res.json();
    try {
      window.localStorage.setItem(cacheKey, JSON.stringify(entries));
    } catch {
      // Quota exhausted: serving this session uncached is still correct.
    }
    return indexCatalogue(entries);
  })();

  // A failed fetch must not be cached as a permanent verdict, or the local-file
  // fallback becomes the only route for the rest of the session.
  promise.catch(() => catalogues.delete(filename));
  catalogues.set(filename, promise);
  return promise;
}

function applyDescriptions(body, field, index) {
  let matched = 0;
  field.row_keys.forEach((key, i) => {
    const entry = index.get(String(key));
    const cell = body.children[i] && body.children[i].lastElementChild;
    if (!entry || !cell) return;
    cell.replaceChildren(el('span', null, entry.l_dsc));
    if (entry.s_dsc) cell.append(el('em', 'muted', entry.s_dsc));
    matched++;
  });
  return matched;
}

// Lets the user supply the catalogue by hand when the device has no route to the internet.
function catalogueFilePicker(field, body, status) {
  const wrap = el('div', 'field');
  const input = el('input');
  input.type = 'file';
  input.accept = '.json';
  input.setAttribute('aria-label', `Load descriptions from a local ${field.catalogue}`);
  input.addEventListener('change', async () => {
    const file = input.files && input.files[0];
    if (!file) return;
    try {
      const entries = JSON.parse(await file.text());
      const matched = applyDescriptions(body, field, indexCatalogue(entries));
      status.textContent = `${matched}/${field.row_keys.length} matched from ${file.name}`;
    } catch {
      status.textContent = `Could not read ${file.name}`;
    }
  });
  wrap.append(el('label', null, `Load ${field.catalogue}`), input);
  return wrap;
}

function describedTable(field, table, body) {
  const group = el('div');
  group.append(table);
  const status = el('div', 'muted');
  group.append(status);

  if (!field.catalogue) {
    status.textContent = 'No description catalogue for this battery.';
    group.append(catalogueFilePicker(field, body, status));
    return group;
  }

  status.textContent = `Loading ${field.catalogue}…`;
  loadCatalogue(field.catalogue).then(
    (index) => {
      const matched = applyDescriptions(body, field, index);
      status.textContent = `${matched}/${field.row_keys.length} descriptions matched`;
    },
    () => {
      status.textContent = `Could not fetch ${field.catalogue}.`;
      group.append(catalogueFilePicker(field, body, status));
    },
  );
  return group;
}

function fieldNode(field) {
  if (field.kind === 'table') {
    const table = el('table', 'data-table');
    const headRow = el('tr');
    field.columns.forEach((c) => headRow.append(el('th', null, c)));
    const thead = el('thead');
    thead.append(headRow);
    table.append(thead);
    const body = el('tbody');
    field.rows.forEach((r) => {
      const tr = el('tr');
      r.forEach((cell) => tr.append(el('td', null, cell)));
      body.append(tr);
    });
    table.append(body);
    if (field.row_keys) return describedTable(field, table, body);
    return table;
  }
  const row = el('div', 'field');
  if (field.sev) row.dataset.severity = field.sev;
  row.append(el('label', null, field.label));
  row.append(el('span', null, field.unit ? `${field.value} ${field.unit}` : field.value));
  return row;
}

async function send(cmd, body, btn) {
  if (cmd.prompt && !window.confirm(`Are you sure you want to ${cmd.prompt}`)) return;
  try {
    await postJson('/api/advanced/command', body);
  } catch {
    window.alert('Command failed.');
    return;
  }
  // A clicked button parks focus, holding the repaint off; release it only on
  // success so a failed command leaves the typed value intact for a retry.
  btn.blur();
  if (cmd.reload_after) reload();
}

function commandButton(cmd, batteryIndex) {
  const btn = el('button', 'btn', cmd.title);
  btn.type = 'button';
  btn.addEventListener('click', () => send(cmd, { id: cmd.id, battery: batteryIndex }, btn));
  return btn;
}

// The wire carries scaled integers; decimal conversion lives here because the device has no FPU.
function commandValueInput(cmd, batteryIndex) {
  const scale = 10 ** cmd.value.decimals;
  const lo = cmd.value.min / scale;
  const hi = cmd.value.max / scale;

  const group = el('div', 'value-command');
  const input = el('input');
  input.type = 'number';
  input.min = lo;
  input.max = hi;
  input.step = 1 / scale;
  input.setAttribute('aria-label', `${cmd.title} (${cmd.value.unit})`);

  const btn = el('button', 'btn', cmd.title);
  btn.type = 'button';

  const submit = () => {
    const entered = Number(input.value);
    const value = Math.round(entered * scale);
    if (input.value === '' || !Number.isFinite(entered) || value < cmd.value.min || value > cmd.value.max) {
      window.alert(`Enter a value between ${lo} and ${hi} ${cmd.value.unit}.`);
      return;
    }
    send(cmd, { id: cmd.id, battery: batteryIndex, value }, btn);
  };

  btn.addEventListener('click', submit);
  input.addEventListener('keydown', (event) => {
    if (event.key !== 'Enter') return;
    event.preventDefault();
    submit();
  });

  group.append(input, el('span', 'value-unit', cmd.value.unit), btn);
  return group;
}

function batterySection(battery, multi) {
  const card = el('div', 'card');
  if (multi) card.append(el('h3', null, `Battery ${battery.index + 1}`));
  if (!battery.sections.length && !battery.commands.length) {
    card.append(el('div', 'muted', 'This battery has no advanced status or commands.'));
    return card;
  }
  battery.sections.forEach((section) => {
    if (section.title) card.append(el('h4', null, section.title));
    section.fields.forEach((f) => card.append(fieldNode(f)));
  });
  if (battery.commands.length) {
    const bar = el('div', 'action-row');
    battery.commands.forEach((c) =>
      bar.append(c.value ? commandValueInput(c, battery.index) : commandButton(c, battery.index)),
    );
    card.append(bar);
  }
  return card;
}

function panelHasFocus() {
  return root.contains(document.activeElement);
}

function paint(data) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', 'Advanced'));
  if (!data.batteries.length) {
    wrap.append(el('div', 'muted', 'No battery configured.'));
  } else {
    const multi = data.batteries.length > 1;
    data.batteries.forEach((b) => wrap.append(batterySection(b, multi)));
  }
  root.replaceChildren(wrap);
}

async function reload() {
  paint(await getJson('/api/advanced'));
}

export async function mount(container) {
  root = container;
  await reload();
  timer = setInterval(() => {
    if (!panelHasFocus()) reload().catch(() => {});
  }, REFRESH_MS);
}

export function render() {}

export function unmount() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
}
