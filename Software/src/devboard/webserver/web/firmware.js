import { t } from '/app.js';

export function mount(container) {
  const title = document.createElement('h1');
  title.className = 'page-title';
  title.textContent = t('ui.firmware', 'Firmware');

  const frame = document.createElement('iframe');
  frame.className = 'firmware-frame';
  frame.src = '/update';
  frame.title = t('ui.firmware_update', 'Firmware update');

  container.replaceChildren(title, frame);
}
