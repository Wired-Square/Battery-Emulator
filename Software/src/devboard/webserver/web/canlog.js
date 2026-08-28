import { getJson, postJson, fetchWithTimeout, t, tf } from '/app.js';

// user_selected_CAN_ID_cutoff_filter is a uint16_t on the device.
const CAN_ID_CUTOFF_MIN = 0;
const CAN_ID_CUTOFF_MAX = 65535;

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

let root = null;

function actionBar(data, reload) {
  const bar = el('div', 'action-row');
  const refresh = el('button', 'btn', t('ui.refresh', 'Refresh'));
  refresh.type = 'button';
  refresh.addEventListener('click', () => reload().catch(() => window.alert(t('ui.canlog_refresh_failed', 'Could not refresh the CAN log.'))));

  const cutoff = el('button', 'btn', tf('ui.edit_canid_cutoff', 'Edit CAN-ID cutoff ({})', data.cutoff));
  cutoff.type = 'button';
  cutoff.addEventListener('click', async () => {
    const entry = window.prompt(
      tf('ui.canid_cutoff_prompt', 'Ignore CAN IDs at or below ({}-{}):', CAN_ID_CUTOFF_MIN, CAN_ID_CUTOFF_MAX),
      String(data.cutoff),
    );
    if (entry === null) return;
    const value = Number(entry);
    if (!Number.isInteger(value) || value < CAN_ID_CUTOFF_MIN || value > CAN_ID_CUTOFF_MAX) {
      window.alert(tf('ui.canid_cutoff_range', 'Enter a whole number between {} and {}.',
                      CAN_ID_CUTOFF_MIN, CAN_ID_CUTOFF_MAX));
      return;
    }
    try {
      await postJson('/api/canidcutoff', { cutoff: value });
    } catch {
      window.alert(t('ui.canid_cutoff_failed', 'Could not update the CAN-ID cutoff.'));
      return;
    }
    reload();
  });

  const exportLink = el('a', 'btn', t('ui.export_txt', 'Export to .txt'));
  exportLink.href = '/export_can_log';

  bar.append(refresh, cutoff, exportLink);
  if (data.sd) {
    const del = el('button', 'btn btn-fault', t('ui.delete_log_file', 'Delete log file'));
    del.type = 'button';
    del.addEventListener('click', async () => {
      if (!window.confirm(t('ui.confirm_delete_canlog', 'Delete the CAN log file on the SD card?'))) return;
      try {
        const res = await fetchWithTimeout('/delete_can_log');
        if (!res.ok) throw new Error();
      } catch {
        window.alert(t('ui.canlog_delete_failed', 'Could not delete the CAN log file.'));
        return;
      }
      reload();
    });
    bar.append(del);
  }
  return bar;
}

function paint(data, reload) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', t('ui.can_log', 'CAN log')));
  wrap.append(actionBar(data, reload));
  const list = el('div', 'log-list');
  if (data.lines.length === 0) list.append(el('div', 'muted', t('ui.no_messages_logged', 'No messages logged yet.')));
  else data.lines.forEach((line) => list.append(el('div', 'can-message', line)));
  wrap.append(list);
  root.replaceChildren(wrap);
}

async function reload() {
  paint(await getJson('/api/canlog'), reload);
}

export async function mount(container) {
  root = container;
  await reload();
}

export function render() {}

export async function unmount() {
  // The router awaits this before the next route mounts, so the stop must be
  // time-bounded or a hung socket would wedge navigation. Leaving is non-fatal:
  // a failed or slow stop just leaves CAN logging running until the next visit.
  try {
    await fetchWithTimeout('/api/canlog/stop', { method: 'POST' });
  } catch {}
}
