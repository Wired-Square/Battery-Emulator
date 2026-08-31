# DTC description files

This folder holds the **DTC (Diagnostic Trouble Code) description** JSON files used by the
Battery-Emulator web UI. When a battery page shows a DTC table, a small JavaScript loader fetches the
matching file from server (GitHub) and fills in a human-readable description for each code.

- **Fetched from:** `https://raw.githubusercontent.com/dalathegreat/Battery-Emulator/main/web_data/dtc/`
  (the `CATALOGUE_BASE_URL` constant in
  [`Software/src/devboard/webserver/web/advanced.js`](../../Software/src/devboard/webserver/web/advanced.js)).
- **Loader code:** `loadCatalogue()` and `describedTable()` in the same file.
- **Firmware side:** `write_dtc_section()` in
  [`Software/src/devboard/webserver/advanced_api.cpp`](../../Software/src/devboard/webserver/advanced_api.cpp),
  which emits the table and the per-row match keys.
- **Validator:** [`tools/validate_dtc_json.py`](../../tools/validate_dtc_json.py).
- **JSON Schema:** [`dtc.schema.json`](dtc.schema.json) (editor autocomplete / inline validation).

> The loader fetches files from GitHub `main`. A new or edited file only takes effect for end users
> **after it is merged to `main`**. Until then, test it locally with the page's file picker (see below).

---

## 1. JSON file format

Each file is a **JSON array of objects**. Every object describes one DTC:

```json
[
  { "code": 41848, "dtc": "P0C9500", "s_dsc": "Diag_S_Temp_01CM11_ORH", "l_dsc": "Temp Sensor \"K\" Circuit High" },
  { "code": 41849, "dtc": "P0C9400", "l_dsc": "Temp Sensor \"K\" Circuit Low" },
  { "code": 132864, "l_dsc": "SME: Transport mode active" },
  { "dtc": "P0A1F00", "s_dsc": "Diag_S_BMS_Internal", "l_dsc": "Hybrid/EV Battery Control Module" },
  { "code": null, "dtc": "U111300", "l_dsc": "Lost Communication With Hybrid Battery Module" }
]
```

Every entry above is valid; each shows a different allowed combination:

| Entry | `code` | `dtc` | `s_dsc` | Matched by | Notes |
|-------|--------|-------|---------|------------|-------|
| 1 | ✅ | ✅ | ✅ | `code` and `dtc` | Fully specified — works whether the cell carries the decimal or the string. |
| 2 | ✅ | ✅ | — *(missing)* | `code` and `dtc` | No `s_dsc`; only the `l_dsc` line is shown. |
| 3 | ✅ | — *(missing)* | — *(missing)* | `code` | Decimal-only entry (e.g. BMW-style file). |
| 4 | — *(missing)* | ✅ | ✅ | `dtc` | String-only entry — no decimal code available. |
| 5 | `null` | ✅ | — *(missing)* | `dtc` | Explicit `null` code is treated the same as omitting it; matched by string. |

`code` and `dtc` are each independently optional **as long as at least one is present**; `s_dsc` is
always optional. Omitting a key and setting it to `null` behave identically for `code`. (Do **not**
use an empty string `""` for `code` — see the rules below.)

### Fields

| Field   | Type    | Required | Purpose |
|---------|---------|----------|---------|
| `code`  | integer | one of `code`/`dtc` | Decimal DTC code. Matches a row whose match key is the decimal code. |
| `dtc`   | string  | one of `code`/`dtc` | DTC string (e.g. `"P0C9500"`). Matches a row whose match key is that string. |
| `s_dsc` | string  | optional | Short/internal description, shown in grey italics under the long one. |
| `l_dsc` | string  | **yes**  | Long human-readable description. This is the text that gets displayed. |

An entry must have **at least one** identifier (`code` or `dtc`) so the loader can match it to a row,
and it must have a non-empty **`l_dsc`** so there is something to show. `s_dsc` is optional — when
empty it is simply omitted from the output (no empty line).

### How matching works

`indexCatalogue()` builds a lookup keyed by **whichever identifier each entry has**, both stringified:

```js
if (e.code !== undefined && e.code !== null) map.set(String(e.code), e);
if (e.dtc) map.set(String(e.dtc), e);
```

The firmware sends one **match key per row** alongside the table, in the `row_keys` array of the
`/api/advanced` payload, and `applyDescriptions()` looks each one up positionally against the rows.
The key is not always what the row displays: it comes from `format_dtc_match_key()`, and its form is
chosen by the `DtcCodeStyle` the driver passes to `write_dtc_section()`.

| `DtcCodeStyle` | Match key | Catalogue field it matches | Example |
|---|---|---|---|
| `kRawHex` | the raw code in **decimal** | `code` | `41848` |
| `kStandard` | full 7-character SAE string | `dtc` | `P0C9500` |
| `kShortFailureType` | first 5 characters, failure type dropped | `dtc` | `P33D7` |

`kShortFailureType` exists because Nissan service data, LeafSpy and `nissan_leaf_dtc.json` all index
on the short form. The failure-type byte is still **displayed** when set (`P33D7-2F`), so nothing is
hidden from the user, but it stays out of the lookup key.

So a single file can mix entries keyed by decimal code, by string, or by both. Key your file to match
the style its driver uses, or supply both.

### Rules and gotchas

- **`l_dsc` is mandatory** and must be non-empty.
- **Provide `code`, `dtc`, or both.** If you only have the string code, omit `code` entirely.
- **Do not use `"code": ""`.** An empty string is *not* the same as absent: `String("")` still becomes
  a junk `""` key in the lookup. To mean "no code", **omit the key** (preferred) or use `"code": null`.
- **`code` must be an integer** (not a quoted string, float, or boolean).
- **Duplicate identifiers = last wins.** If two entries share the same `code` (or the same `dtc` when
  `dtc` is the only key), the later one overwrites the earlier in the lookup. Decimal `code` is the
  unambiguous key; the same `dtc` string can legitimately map to several distinct codes (different
  sensors), so prefer matching by `code` whenever codes are available.
- **Empty `s_dsc` is fine** — it is just skipped in the rendered output.
- Files are **UTF-8**; escape `"` inside strings as `\"` (standard JSON).
- These files are fetched and cached by the browser, so keep them lean: drop fields you don't use
  (e.g. a string-only file can be just `{ "dtc": "...", "l_dsc": "..." }`).

### Caching / refreshing

The loader caches each file in the browser's `localStorage` under `catalogue:<filename>`.

**The cache has no expiry and there is no refresh control.** `loadCatalogue()` returns the stored copy
whenever one is present and only reaches the network when it is absent, so a browser that has already
loaded a catalogue will not pick up an edit to it on `main`. Clear the site's local storage to force a
refetch. A failed fetch is deliberately not cached, so a network problem does not become permanent for
the session.

If the fetch fails, or the battery declares no catalogue at all, the page reveals a **file picker** so
a local copy can be loaded by hand. That is the way to test a file before it is merged.

---

## 2. Validating a file

Run the validator before committing. It encodes all the rules above and checks JSON validity, missing
or empty `l_dsc`, unusable/duplicate keys, `code` typing, the empty-string-`code` trap, unknown keys,
and more.

```bash
# one file
python tools/validate_dtc_json.py web_data/dtc/bmw_phev_dtc.json

# all of them
python tools/validate_dtc_json.py web_data/dtc/*.json

# treat warnings as failures (good for CI / pre-commit)
python tools/validate_dtc_json.py --strict web_data/dtc/*.json

# only print files that have problems
python tools/validate_dtc_json.py -q web_data/dtc/*.json
```

Exit code is `0` when clean and `1` when any file has errors (or warnings under `--strict`), so it
drops straight into CI or a git hook. Errors point at the offending entry by index plus its
`code`/`dtc`/`l_dsc`, e.g.:

```
ERROR  : #8 (code=5): missing or empty 'l_dsc'
WARNING: duplicate 'code' 100 in entries [0, 1] (later overwrites earlier in lookup)
```

### JSON Schema (editor support)

[`dtc.schema.json`](dtc.schema.json) describes the format for editors and external tooling. It gives
you autocomplete and red-squiggle validation while typing (required `l_dsc`, integer-only `code`,
"at least one identifier", no unknown keys, etc.).

Because each file's root is a JSON **array**, you can't point at the schema with an inline `$schema`
key — map it in your editor instead. For VS Code, add to `.vscode/settings.json`:

```json
{
  "json.schemas": [
    {
      "fileMatch": ["web_data/dtc/*_dtc.json"],
      "url": "./web_data/dtc/dtc.schema.json"
    }
  ]
}
```

You can also validate against it from the command line (the schema gives quick structural feedback;
`validate_dtc_json.py` adds the cross-entry and "last-wins" checks the schema can't express):

```bash
python -c "import json,sys,glob; from jsonschema import Draft202012Validator as V; \
s=json.load(open('web_data/dtc/dtc.schema.json')); v=V(s); \
[print(f,'OK' if not list(v.iter_errors(json.load(open(f)))) else 'INVALID') \
 for f in glob.glob('web_data/dtc/*_dtc.json')]"
```

> Note: the schema flags unknown keys as **errors** (stricter than the validator, which treats them as
> warnings) to catch typos early while authoring.

---

## 3. Wiring a driver to a catalogue

A battery driver does two things: it names its catalogue file, and it calls the shared DTC section
writer from its advanced-status output. It never emits markup, and it never touches the loader.

**Name the file** in the driver header, overriding the default (empty, meaning "no catalogue"):

```cpp
// Software/src/battery/BMW-IX-BATTERY.h
const char* get_dtc_json_filename() override { return "bmw_ix_dtc.json"; }
```

**Emit the section** from `write_advanced_status()`, choosing the code style your JSON is keyed for:

```cpp
// Software/src/battery/NISSAN-LEAF-BATTERY.cpp, at the end of the file
void NissanLeafBattery::write_advanced_status(AdvancedStatusWriter& out) {
  // ... other sections ...
  write_dtc_section(out, *this, datalayer_battery->dtc, DtcCodeStyle::kShortFailureType);
}
```

`write_dtc_section()` renders the whole section: a status line when the store is unread, failed or
empty, and otherwise a DTC/Status/Description table with the description column filled in by the
client. It reads `get_dtc_json_filename()` off the battery you pass it, so the two stay in step.

A driver whose DTC store is not the shared `DATALAYER_BATTERY_DTC_TYPE` copies its codes into a local
one rather than reimplementing the section. `BMW-IX-BATTERY.cpp` does this.

Per the driver file layout convention, `write_advanced_status()` is declared in the header and defined
at the **end** of the `.cpp`, advanced status first and `settings()` second.

---

## 4. Adding DTC descriptions for a new battery

1. **Create the JSON file** in this folder, e.g. `web_data/dtc/<battery>_dtc.json`, following the
   format in section 1. Decide whether it is keyed by decimal `code` or by the `dtc` string, and pick
   the `DtcCodeStyle` in section "How matching works" that produces the same key (or supply both).
2. **Validate it:** `python tools/validate_dtc_json.py web_data/dtc/<battery>_dtc.json` (fix all
   errors; review warnings).
3. **Override `get_dtc_json_filename()`** in the driver header to return the filename.
4. **Call `write_dtc_section()`** from the driver's `write_advanced_status()` with the matching style.
5. **Test locally before merge:** flash the firmware, open the battery's advanced page, and use the
   **file picker** to load your local JSON. GitHub `main` will not have it yet, and once the real file
   is merged your browser will keep serving whatever it cached, so clear local storage when you switch
   from the picker to the fetched copy. Confirm the status line reports the match count you expect.
6. **Commit both** the JSON file and the driver change, and merge to `main` so the fetch works for
   everyone.
