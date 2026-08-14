import { getJson, fetchWithTimeout } from '/app.js';

const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

let root = null;

function paint(data, reload) {
  const wrap = el('div');
  wrap.append(el('h1', 'page-title', 'Debug log'));
  const bar = el('div', 'action-row');
  if (data.web_active) {
    const refresh = el('button', 'btn', 'Refresh');
    refresh.type = 'button';
    refresh.addEventListener('click', () => reload().catch(() => window.alert('Could not refresh the debug log.')));
    bar.append(refresh);
  }
  const exportLink = el('a', 'btn', 'Export to .txt');
  exportLink.href = '/export_log';
  bar.append(exportLink);
  if (data.sd_active) {
    const del = el('button', 'btn btn-fault', 'Delete log file');
    del.type = 'button';
    del.addEventListener('click', async () => {
      if (!window.confirm('Delete the log file on the SD card?')) return;
      try {
        const res = await fetchWithTimeout('/delete_log');
        if (!res.ok) throw new Error();
      } catch {
        window.alert('Could not delete the log file.');
        return;
      }
      reload();
    });
    bar.append(del);
  }
  wrap.append(bar);
  const pre = el('pre', 'log-pre', data.lines.join('\n'));
  wrap.append(pre);
  root.replaceChildren(wrap);
}

async function reload() {
  paint(await getJson('/api/debug'), reload);
}

export async function mount(container) {
  root = container;
  await reload();
}

export function render() {}
