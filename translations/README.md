# Web UI translations

`en.json` in this folder is **generated**. Do not edit it by hand: the next build overwrites it.

English is not a language pack. Every user-visible string ships inline in the firmware sources as the
fallback argument of a `t()`, `tf()` or `TL()` call, so a device with no network still renders in
English. This file is the extract of those fallbacks, and it is what translators work from.

## Regenerating

```bash
python3 scripts/gen_i18n_en.py     # writes translations/en.json
```

It also runs automatically as a PlatformIO pre-build `extra_script`, so a build with a new or changed
string leaves this file dirty. Commit it with the change that caused it. `scripts/test_gen_i18n_en.py`
covers the extractor and runs in the script suite:

```bash
(cd scripts && python3 -m unittest test_gen_i18n_en)
```

The extractor is stdlib only, so every environment and any CI can build without `pip`.

## Where the strings come from

`gen_i18n_en.py` scans both sides of the UI: the browser sources under
`Software/src/devboard/webserver/web/`, and the firmware sources that supply text the client cannot
know (`settings_api.cpp`, `advanced_api.cpp`, `events.cpp`, and the battery and inverter drivers).

| Namespace | Count | Source |
|---|---|---|
| `adv.` | 655 | driver advanced-status labels, via `TL()` |
| `ui.` | 156 | client chrome and messages |
| `setting.` | 148 | settings rows, keyed on the NVS key |
| `event.` | 137 | event messages from `events.cpp` |
| `row.` | 44 | advanced-status table headings |
| `error.` | 11 | POST rejection keys returned as `error_key` |
| `category.`, `live.`, `help.`, `time.` | 26 | settings page structure |

A driver adding a status field with `out.kv(TL("Cell balance time"), ...)` adds an `adv.` key on the
next build.

`adv.` keys are the only ones derived from the text itself: `advanced_slug()` in the extractor and
`advText()` in `advanced.js` must agree, so the key for `"Cell balance time"` is
`adv.cell_balance_time`. **Rewording a `TL()` string therefore retires its key and creates a new
one**, and every existing translation of that string is dropped. Reword deliberately.

The other namespaces are keyed on stable identifiers and are safe to reword: `setting.` on the NVS
key, `event.` on the event enum name, and `ui.` and `row.` on the literal key passed to `t()`.

## How a translated UI is served

Nothing is served from the device. The browser fetches packs directly from the public translation
repository, so adding a language needs no firmware release and costs no flash.

The index is `https://wired-square.github.io/battery-emulator-i18n/index.json`
(`I18N_INDEX_URL` in `Software/src/devboard/webserver/web/app.js`):

```json
{
  "revision": "2026-08-27T04:11:00Z",
  "languages": [
    { "code": "de", "name": "Deutsch", "path": "packs/de.json" }
  ]
}
```

- `revision` is an opaque string. The client stores the last one it fully applied and skips the whole
  sync when it is unchanged, so a stable revision means no pack traffic on later loads.
- `path` resolves relative to the index URL, and a pack that resolves off that origin is rejected.
- An entry needs `code` and `path` to be strings or it is ignored. `name` falls back to `code`.

A pack is a flat JSON object of key to string, the same shape as `en.json`. Non-string values are
dropped, and a pack with no usable strings is rejected rather than stored. Missing keys fall back to
the English text compiled into the firmware, so a partial pack is valid and useful.

Limits the client enforces (`app.js`):

| Limit | Value | Effect when exceeded |
|---|---|---|
| `MAX_PACK_BYTES` | 262144 | that pack is skipped |
| `MAX_LANGUAGES` | 64 | languages past the 64th are not downloaded |

Any pack that fails leaves `revision` unrecorded, so the next load retries.

## Browser storage

Packs are cached in `localStorage`, one key per language, and survive reloads and reboots:

| Key | Holds |
|---|---|
| `be-lang` | the selected language code |
| `be-i18n-<code>` | `{"name": "...", "strings": {...}}` for one language |
| `be-translations-revision` | the last index revision applied in full |

Storage is per browser, so each device and each browser syncs independently. A full storage quota is
not fatal: the session runs untranslated rather than failing.

Picking a language from the menu reloads the page. The background sync deliberately does not: a
reload mid-edit would discard unsaved settings, so a pack that arrives while the settings page is
open applies on the next navigation.

## Adding a language

1. Copy `en.json` and translate the values. Leave keys untouched. Partial packs are fine.
2. Publish it in the translation repository and add its entry to `index.json`.
3. Change `revision` in the same commit. Clients that already synced will not look again until it
   changes.
4. Load the web UI and pick the language. If it does not appear, check the browser console: an
   off-origin path, an oversized pack, or a pack with no string values is skipped silently by design,
   so that one bad pack cannot stop the rest from syncing.
