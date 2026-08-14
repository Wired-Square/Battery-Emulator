export function mount(container) {
  const title = document.createElement('h1');
  title.className = 'page-title';
  title.textContent = 'Firmware';

  const frame = document.createElement('iframe');
  frame.className = 'firmware-frame';
  frame.src = '/update';
  frame.title = 'Firmware update';

  container.replaceChildren(title, frame);
}
