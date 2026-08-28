import { getJson, postJson, fetchWithTimeout, t } from '/app.js';

const REFRESH_MS = 2000;
const IMPORT_URL = '/import_can_log';

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

let fieldSeq = 0;
const field = (labelText, control) => {
  const wrap = el('div', 'field');
  control.id = `canreplay-f${fieldSeq++}`;
  const label = el('label', null, labelText);
  label.htmlFor = control.id;
  wrap.append(label, control);
  return wrap;
};

const cardHead = (title, trailing) => {
  const head = el('div', 'card-head');
  head.append(el('h2', null, title));
  if (trailing) head.append(trailing);
  return head;
};

let root = null;
let timer = null;
let selectedFile = null;
// Structure is built once; the poll updates only these run-state controls so it
// never clobbers the file selection, preview, or loop checkbox mid-edit.
let startBtn = null;
let stopBtn = null;
let statusPill = null;
let hasLog = false;

function applyRunState(data) {
  hasLog = data.has_log;
  startBtn.disabled = data.running || !hasLog;
  stopBtn.disabled = !data.running;
  statusPill.textContent = data.running ? t('ui.running', 'Running') : t('ui.stopped', 'Stopped');
  statusPill.className = `pill ${data.running ? 'pill-on' : 'pill-off'}`;
}

async function refresh() {
  applyRunState(await getJson('/api/canreplay'));
}

function interfaceSection(data) {
  const section = el('div', 'card card-rows card-aligned');
  section.append(cardHead(t('ui.playback_interface', 'Playback interface')));

  const select = el('select');
  data.interfaces.forEach((iface) => {
    const opt = el('option', null, iface.name);
    opt.value = String(iface.index);
    if (iface.index === data.selected) opt.selected = true;
    select.append(opt);
  });
  const none = !data.interfaces.length;
  if (none) {
    select.append(el('option', null, t('ui.no_can_interface', 'No CAN interface available')));
    select.disabled = true;
  }
  section.append(field(t('ui.can_interface', 'CAN interface'), select));

  const apply = el('button', 'btn', t('ui.apply', 'Apply'));
  apply.type = 'button';
  apply.disabled = none;
  apply.addEventListener('click', async () => {
    try {
      applyRunState(await postJson('/api/canreplay/interface', { interface: Number(select.value) }));
    } catch {
      window.alert(t('ui.interface_select_failed', 'Could not select the interface.'));
    }
  });
  const bar = el('div', 'action-row');
  bar.append(apply);
  section.append(bar);
  return section;
}

function uploadSection() {
  const section = el('div', 'card card-rows card-aligned');
  section.append(cardHead(t('ui.log_file', 'Log file')));

  const input = el('input');
  input.type = 'file';
  input.accept = '.txt';
  input.addEventListener('change', () => {
    selectedFile = input.files[0] ?? null;
  });
  section.append(field(t('ui.candump_txt', 'CANdump .txt'), input));

  const preview = el('pre', 'log-pre');
  preview.hidden = true;

  const upload = el('button', 'btn', t('ui.upload', 'Upload'));
  upload.type = 'button';
  upload.addEventListener('click', async () => {
    if (!selectedFile) {
      window.alert(t('ui.select_txt_first', 'Select a .txt log file first.'));
      return;
    }
    const body = new FormData();
    body.append('file', selectedFile);
    try {
      const res = await fetchWithTimeout(IMPORT_URL, { method: 'POST', body });
      if (!res.ok) throw new Error();
    } catch {
      window.alert(t('ui.upload_failed', 'Upload failed.'));
      return;
    }
    preview.textContent = await selectedFile.text();
    preview.hidden = false;
    section.classList.add('card-aligned-preview');
    refresh().catch(() => {});
  });
  const bar = el('div', 'action-row');
  bar.append(upload);
  section.append(bar, preview);
  return section;
}

function playbackSection(data) {
  const section = el('div', 'card card-rows card-aligned');

  statusPill = el('span', 'pill pill-off', t('ui.stopped', 'Stopped'));
  section.append(cardHead(t('ui.playback', 'Playback'), statusPill));

  const loop = el('input');
  loop.type = 'checkbox';
  loop.checked = data.loop;
  section.append(field(t('ui.loop', 'Loop'), loop));

  startBtn = el('button', 'btn btn-ok', t('ui.start', 'Start'));
  startBtn.type = 'button';
  startBtn.addEventListener('click', async () => {
    try {
      applyRunState(await postJson('/api/canreplay/start', { loop: loop.checked }));
    } catch {
      window.alert(t('ui.replay_start_failed', 'Could not start replay (already running?).'));
    }
  });

  stopBtn = el('button', 'btn btn-fault', t('ui.stop', 'Stop'));
  stopBtn.type = 'button';
  stopBtn.addEventListener('click', async () => {
    try {
      applyRunState(await postJson('/api/canreplay/stop', {}));
    } catch {
      window.alert(t('ui.replay_stop_failed', 'Could not stop replay.'));
    }
  });

  const bar = el('div', 'action-row');
  bar.append(startBtn, stopBtn);
  section.append(bar);
  return section;
}

function paint(data) {
  const cards = el('div', 'cards');
  cards.append(interfaceSection(data), uploadSection(), playbackSection(data));
  root.replaceChildren(el('h1', 'page-title', t('ui.can_replay', 'CAN replay')), cards);
  applyRunState(data);
}

export async function mount(container) {
  root = container;
  paint(await getJson('/api/canreplay'));
  timer = setInterval(() => refresh().catch(() => {}), REFRESH_MS);
}

export function render() {}

export function unmount() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
  selectedFile = null;
}
