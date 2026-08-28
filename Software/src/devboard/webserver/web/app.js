const POLL_INTERVAL_MS = 1500;
const STALE_AFTER_MS = 5000;
// Slack for the reboot gap check: covers RTT, poll jitter, and uptime_s truncation.
const REBOOT_GAP_SLACK_MS = 2000;
const THEME_KEY = 'be-theme';
const LANG_KEY = 'be-lang';
const PACK_KEY_PREFIX = 'be-i18n-';
const REVISION_KEY = 'be-translations-revision';
const SOURCE_LANGUAGE = 'en';
const I18N_INDEX_URL = 'https://wired-square.github.io/battery-emulator-i18n/index.json';
const MAX_PACK_BYTES = 262144;
const MAX_LANGUAGES = 64;

export const skinName = document.documentElement.dataset.skin ?? 'modern';

let capabilities = {};
export function deviceCapabilities() {
  return capabilities;
}

let dictionary = {};

export function t(key, fallback) {
  const translated = dictionary[key];
  return typeof translated === 'string' && translated.length > 0 ? translated : (fallback ?? key);
}

export function tf(key, fallback, ...args) {
  let text = t(key, fallback);
  for (const arg of args) text = text.replace('{}', () => String(arg));
  return text;
}

function readStored(key) {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function writeStored(key, value) {
  try {
    localStorage.setItem(key, value);
    return true;
  } catch {
    return false;
  }
}

export function currentLanguage() {
  return readStored(LANG_KEY) ?? SOURCE_LANGUAGE;
}

function usableStrings(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const strings = {};
  for (const [key, text] of Object.entries(value)) {
    if (typeof text === 'string') strings[key] = text;
  }
  return Object.keys(strings).length > 0 ? strings : null;
}

function storedPack(lang) {
  const raw = readStored(PACK_KEY_PREFIX + lang);
  if (!raw) return null;
  try {
    const pack = JSON.parse(raw);
    return pack && typeof pack.name === 'string' && pack.strings ? pack : null;
  } catch {
    return null;
  }
}

export function cachedLanguages() {
  const codes = [];
  try {
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i);
      if (key && key.startsWith(PACK_KEY_PREFIX)) codes.push(key.slice(PACK_KEY_PREFIX.length));
    }
  } catch {
    /* ignored */
  }
  return codes.sort().map((code) => ({ code, name: storedPack(code)?.name ?? code }));
}

function applyLanguage(lang) {
  document.documentElement.lang = lang;
  dictionary = lang === SOURCE_LANGUAGE ? {} : (storedPack(lang)?.strings ?? {});
  translateShell();
}

async function fetchLanguageIndex() {
  const res = await fetchWithTimeout(I18N_INDEX_URL, { cache: 'no-cache' });
  if (!res.ok) throw new Error(`${I18N_INDEX_URL} -> ${res.status}`);
  const index = await res.json();
  const languages = Array.isArray(index?.languages) ? index.languages : [];
  return {
    revision: typeof index?.revision === 'string' ? index.revision : '',
    languages: languages.filter((entry) => typeof entry?.code === 'string' && typeof entry?.path === 'string'),
  };
}

async function downloadLanguage(entry) {
  const url = new URL(entry.path, I18N_INDEX_URL);
  if (url.origin !== new URL(I18N_INDEX_URL).origin) throw new Error('pack is off-origin');
  const res = await fetchWithTimeout(url.href, { cache: 'no-cache' });
  if (!res.ok) throw new Error(`${entry.path} -> ${res.status}`);
  const body = await res.text();
  if (body.length > MAX_PACK_BYTES) throw new Error('pack too large');
  const strings = usableStrings(JSON.parse(body));
  if (!strings) throw new Error('pack has no usable strings');
  const stored = JSON.stringify({ name: entry.name ?? entry.code, strings });
  const changed = stored !== readStored(PACK_KEY_PREFIX + entry.code);
  if (!writeStored(PACK_KEY_PREFIX + entry.code, stored)) {
    throw new Error('no room to store this pack');
  }
  return changed;
}

// Deliberately never reloads: a refresh mid-edit would discard unsaved settings.
async function syncTranslations() {
  let index;
  try {
    index = await fetchLanguageIndex();
  } catch {
    return;
  }
  if (index.revision && index.revision === readStored(REVISION_KEY)) {
    return;
  }
  let complete = true;
  for (const entry of index.languages.slice(0, MAX_LANGUAGES)) {
    try {
      await downloadLanguage(entry);
    } catch {
      complete = false;
    }
  }
  if (complete && index.languages.length <= MAX_LANGUAGES) {
    writeStored(REVISION_KEY, index.revision);
  }
  window.dispatchEvent(new CustomEvent('be-languages-changed'));
}

export function setLanguage(lang) {
  if (writeStored(LANG_KEY, lang)) {
    location.reload();
  }
}

function translateShell() {
  for (const node of document.querySelectorAll('[data-i18n]')) {
    node.textContent = t(node.dataset.i18n, node.textContent);
  }
  for (const node of document.querySelectorAll('[data-i18n-attr]')) {
    const [attr, key] = node.dataset.i18nAttr.split(':');
    node.setAttribute(attr, t(key, node.getAttribute(attr)));
  }
}

const SECONDS_PER_MINUTE = 60;
const SECONDS_PER_HOUR = 3600;
const SECONDS_PER_DAY = 86400;

export function uptimeText(seconds) {
  if (typeof seconds !== 'number') return null;
  return tf('time.uptime', '{} days, {} hours, {} minutes, {} seconds',
            Math.floor(seconds / SECONDS_PER_DAY),
            Math.floor((seconds % SECONDS_PER_DAY) / SECONDS_PER_HOUR),
            Math.floor((seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE),
            seconds % SECONDS_PER_MINUTE);
}

const routes = {
  '/': () => import('/dashboard.js'),
  '/settings': () => import('/settings.js'),
  '/cellmonitor': () => import('/cellmonitor.js'),
  '/advanced': () => import('/advanced.js'),
  '/events': () => import('/events.js'),
  '/canlog': () => import('/canlog.js'),
  '/log': () => import('/debug.js'),
  '/canreplay': () => import('/canreplay.js'),
  '/firmware': () => import('/firmware.js'),
};

let activeRoute = null;
let navSeq = 0;
let navChain = Promise.resolve();
let lastOkAt = 0;
let pollInFlight = false;
let lastUptimeS = null;
let lastUptimeAt = 0;

// A socket that hangs past the staleness deadline can no longer deliver
// anything fresh, so fail it rather than let a caller wait forever.
export function fetchWithTimeout(url, opts = {}) {
  return fetch(url, { credentials: 'same-origin', ...opts, signal: AbortSignal.timeout(STALE_AFTER_MS) });
}

export async function getJson(url) {
  const res = await fetchWithTimeout(url);
  if (!res.ok) throw new Error(`${url} -> ${res.status}`);
  return res.json();
}

export async function postJson(url, body) {
  const res = await fetchWithTimeout(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const err = new Error(`${url} -> ${res.status}`);
    err.body = await res.text().catch(() => '');
    throw err;
  }
  return res.json();
}

function applyTheme(theme) {
  if (theme) document.documentElement.dataset.theme = theme;
  else delete document.documentElement.dataset.theme;
}

// Falls back to the OS preference so the first toggle flips away from what
// the user is actually looking at, not from a hardcoded default.
function currentTheme() {
  return localStorage.getItem(THEME_KEY)
    ?? (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
}

function bindUiSwitch() {
  for (const btn of document.querySelectorAll('[data-ui-switch]')) {
    btn.addEventListener('click', async () => {
      btn.disabled = true;
      try {
        await postJson('/api/settings', { values: { WEBUI: btn.dataset.uiSwitch } });
        location.href = '/';
      } catch (err) {
        console.error('ui switch failed', err);
        btn.disabled = false;
      }
    });
  }
}

function bindChrome() {
  bindUiSwitch();
  const toggle = document.getElementById('theme-toggle');
  if (!toggle) return;
  applyTheme(localStorage.getItem(THEME_KEY));
  toggle.addEventListener('click', () => {
    const next = currentTheme() === 'dark' ? 'light' : 'dark';
    localStorage.setItem(THEME_KEY, next);
    applyTheme(next);
  });
}

function markHeartbeat(ok) {
  const el = document.getElementById('heartbeat');
  if (ok) lastOkAt = Date.now();
  if (!el) return;
  const stale = Date.now() - lastOkAt > STALE_AFTER_MS;
  el.classList.toggle('live', !stale);
  el.classList.toggle('stale', stale);
  // Colour alone is not a status for anyone using a screen reader.
  el.setAttribute('aria-label', stale ? t('ui.device_not_responding', 'Device not responding') : t('ui.live', 'Live'));
}

// Routes repaint from any state document, whether it came from the poll or
// straight back from an action — that is what makes an action feel instant.
export function repaint(state) {
  if (activeRoute && activeRoute.render) activeRoute.render(state);
}

// Transport health is settled before rendering, so a throwing route reports
// as the render bug it is instead of a device that stopped answering.
async function poll() {
  if (pollInFlight) return;
  pollInFlight = true;
  let state;
  try {
    state = await getJson('/api/state');
    markHeartbeat(true);
  } catch {
    markHeartbeat(false);
    return;
  } finally {
    pollInFlight = false;
  }
  // Reload on reboot so the SPA gets fresh assets + firmware version, not a stale page. The gap
  // term also catches a reboot whose outage outlasted the last-seen uptime, which a bare backwards
  // comparison would miss.
  const now = Date.now();
  const uptimeS = state.system?.uptime_s;
  if (typeof uptimeS === 'number') {
    const gapMs = lastUptimeAt ? now - lastUptimeAt : 0;
    const rebooted =
      lastUptimeS !== null && (uptimeS < lastUptimeS || uptimeS * 1000 + REBOOT_GAP_SLACK_MS < gapMs);
    if (rebooted) {
      location.reload();
      return;
    }
    lastUptimeS = uptimeS;
    lastUptimeAt = now;
  }
  repaint(state);
  const navLog = document.getElementById('nav-log');
  if (navLog) navLog.hidden = state.system?.log_available !== true;
}

// Shown when a route cannot load or mount, so a failed navigation never leaves
// a stale page that claims to be the destination. Clears the rail highlight
// because nothing is the current route.
function showRouteError() {
  const content = document.getElementById('content');
  content.replaceChildren();
  content.textContent = t('ui.view_failed', 'This view failed to load.');
  for (const item of document.querySelectorAll('[data-route]')) {
    item.removeAttribute('aria-current');
  }
}

// Navigations are serialised through navChain (see navigate), so router bodies
// never overlap; the nav token still lets an in-flight transition bail the
// moment a newer navigation supersedes it.
async function router(nav) {
  if (nav !== navSeq) return;
  const hash = location.hash.replace(/^#/, '') || '/';
  const loader = routes[hash];
  if (!loader) { location.hash = '#/'; return; }

  // Load the next module before tearing down the current route: a failed
  // import (a route whose module has not shipped yet, or a Wi-Fi blip) leaves
  // the live page intact instead of stranding a half-torn route.
  let next;
  try {
    next = await loader();
  } catch (err) {
    console.error(`route ${hash} failed to load`, err);
    // Only when this is still the latest navigation and an earlier superseded
    // run already tore down its route: replace the orphaned page rather than
    // leave it silently frozen. The common case (import fails while a route is
    // still mounted) keeps the live page.
    if (nav === navSeq && !activeRoute) showRouteError();
    return;
  }
  if (nav !== navSeq) return;

  // Null activeRoute before teardown so poll() cannot render across the gap;
  // isolate unmount so a device-side stop call that fails mid-reset cannot
  // wedge navigation.
  const previous = activeRoute;
  activeRoute = null;
  if (previous?.unmount) {
    try { await previous.unmount(); }
    catch (err) { console.error('route unmount failed', err); }
  }
  if (nav !== navSeq) return;

  const content = document.getElementById('content');
  content.replaceChildren();
  let mounted = true;
  try {
    if (next.mount) await next.mount(content);
  } catch (err) {
    console.error(`route ${hash} failed to mount`, err);
    showRouteError();
    mounted = false;
  }

  // A navigation that arrived while this one was mounting owns the outcome:
  // tear down the route just mounted so its timer does not leak, and let the
  // newer run take over.
  if (nav !== navSeq) {
    if (mounted && next.unmount) {
      try { await next.unmount(); }
      catch (err) { console.error('route unmount failed', err); }
    }
    return;
  }
  if (!mounted) return;

  for (const item of document.querySelectorAll('[data-route]')) {
    if (item.dataset.route === hash) item.setAttribute('aria-current', 'page');
    else item.removeAttribute('aria-current');
  }
  activeRoute = next;
  // Not awaited: poll() paints the new route asynchronously; awaiting it here
  // would hold the serialised nav chain for a full poll (up to the staleness
  // timeout) and make the next navigation feel wedged on a slow link.
  poll();
}

// Identity is cosmetic; a failure here must not stop the dashboard.
async function initIdentity() {
  try {
    const caps = await getJson('/capabilities');
    capabilities = caps;
    const hw = document.getElementById('hw');
    if (!hw) return;
    hw.textContent = caps.hardware ?? '';
    const fw = document.getElementById('fw');
    if (caps.firmware_url) {
      const link = document.createElement('a');
      link.href = caps.firmware_url;
      link.target = '_blank';
      link.rel = 'noopener';
      link.textContent = caps.firmware ?? '';
      fw.replaceChildren(link);
    } else {
      fw.textContent = caps.firmware ?? '';
    }
  } catch {
    /* ignored */
  }
}

bindChrome();
// Serialise navigations so overlapping transitions cannot interleave their
// teardown/mount; the token is bumped synchronously so a superseded run is
// skipped rather than mounted.
function navigate() {
  const nav = ++navSeq;
  navChain = navChain
    .then(() => router(nav))
    .catch((err) => console.error('navigation failed', err));
  return navChain;
}
window.addEventListener('hashchange', navigate);
applyLanguage(currentLanguage());
navigate();
initIdentity();
setInterval(poll, POLL_INTERVAL_MS);
syncTranslations();
