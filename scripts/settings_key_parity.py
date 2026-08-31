#!/usr/bin/env python3
"""NVS key ownership parity between the boot load path and the settings table.

comm_nvm.cpp reads settings straight from NVS at boot; the settings tables describe
what the web API can read and write. A key present in one and absent from the
other is how the two drift apart, which is the class of bug upstream #2697 and
#2839 both fixed.

Device settings are declared by the driver that owns them, so the table scan
covers every DeviceSetting array under the driver directories as well as the
central kSettingFields, the way driver_command_parity.py already scans all
driver headers.

Not every boot key belongs in the table: OWNED_ELSEWHERE names the writer of each
one that does not, so a key left unowned fails the run.

Board rows are declared on the HAL and applied at boot generically, so one table
drives both directions and they cannot drift. What still bites there is the NVS
key-length limit, which a scoped row only hits once its entry index is appended:
board keys are therefore expanded to their widest index before the length check.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "Software/src"
NVM = SRC / "communication/nvm/comm_nvm.cpp"
TABLE = SRC / "devboard/webserver/settings_api.cpp"
DRIVER_DIRS = ("battery", "inverter", "charger")
BOARD_DIR = SRC / "devboard/hal"

# Boot keys the settings table deliberately does not describe, and who owns them.
OWNED_ELSEWHERE = {
    "EQUIPMENT_STOP": "POST /api/equipmentstop",
    "IFSCHEMA": "interface schema, written by the webserver",
    "HADISCFW": "MQTT discovery client",
    "BATTTYPE": "settings POST, dynamic.batteries",
    "BATT2TYPE": "settings POST, dynamic.batteries",
    "BATT3TYPE": "settings POST, dynamic.batteries",
    "CNTCTRL": "settings POST, dynamic.batteries",
    "CNTCTRLDBL": "settings POST, dynamic.batteries",
    "CNTCTRLTRI": "settings POST, dynamic.batteries",
}

# Table entries with no boot read: written at runtime, or read by another module.
NOT_READ_AT_BOOT = {
    "WEBUI": "read by the web UI shell selector",
    "INVCOMM": "packed interface config, resolved by the interface schema",
    "CHGCOMM": "packed interface config, resolved by the interface schema",
    "SHUNTCOMM": "packed interface config, resolved by the interface schema",
    "CANFDASCAN": "read by the HAL during interface setup",
    "CANFD2ASCAN": "read by the HAL during interface setup",
}

BOOT_READ = re.compile(r'settings\.get[A-Za-z]+\("([A-Za-z0-9_]+)"')
BOARD_DECL = re.compile(r"constexpr DeviceSetting k\w+\[\] = \{")
SCOPE_IN_ROW = re.compile(r"SettingScope::(\w+)")
INTERFACE_TABLE = re.compile(r"InterfaceDescriptor k\w+\[\] = \{(.*?)\n\};", re.S)
LOAD_SWITCH_CHANNELS = ("devboard/hal/LoadSwitch.h", "kLoadSwitchConfigChannels")
TABLE_DECLS = ("constexpr SettingField kSettingFields[] = {", "constexpr DeviceSetting kFamilySettingFields[] = {")
DRIVER_DECL = "constexpr DeviceSetting kSettings[] = {"
ROW_KEY = re.compile(r'setting\("([A-Za-z0-9_]+)",\s*(?:ST|SettingType)::')
VOLATILE_ROWS = ("SS::Volatile", "SettingStorage::Volatile", ".volatile_storage()")
NVS_KEY_MAX = 15


def split_rows(body: str) -> list:
    """Table body into rows, splitting on the commas that separate them.

    Rows are constexpr helper calls now, not brace literals, so a row boundary is
    a top-level comma rather than a matching brace.
    """
    rows, depth, cur, i = [], 0, "", 0
    while i < len(body):
        ch = body[i]
        if ch == '"':
            j = i + 1
            while j < len(body) and body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            cur += body[i:j + 1]
            i = j + 1
            continue
        if depth == 0 and ch == "}":
            break
        if ch in "{([":
            depth += 1
        elif ch in "})]":
            depth -= 1
        if ch == "," and depth == 0:
            rows.append(cur)
            cur = ""
        else:
            cur += ch
        i += 1
    rows.append(cur)
    return rows


def table_keys(source: str, decl: str) -> set:
    """Keys of the NVS-backed rows. Volatile rows name no NVS key at all.

    Rows are found by brace depth, not by a regex over the whole file: a row can
    span several lines and can follow a comment or an #if. A line-anchored pattern
    misses those rows silently, and the gate then reports their keys as read at
    boot but settable nowhere.
    """
    if decl not in source:
        return set()
    body = source[source.index(decl) + len(decl):]
    return {m.group(1) for row in split_rows(body)
            for m in [ROW_KEY.search(row)] if m and not any(v in row for v in VOLATILE_ROWS)}


def constant_value(relative_path: str, name: str) -> int:
    source = (SRC / relative_path).read_text()
    match = re.search(rf"{name} = (\d+);", source)
    if match is None:
        raise SystemExit(f"{name} not found in {relative_path}")
    return int(match.group(1))


def scope_size(source: str, scope: str) -> int:
    """How many entries the board's scope_entries() enumerates in this scope."""
    if scope == "LoadSwitchChannel":
        return constant_value(*LOAD_SWITCH_CHANNELS)
    if scope == "Interface":
        tables = [table.count("\n    {") for table in INTERFACE_TABLE.findall(source)]
        return max(tables) if tables else 0
    return 0


def scoped_rows(source: str, decl: re.Pattern) -> set:
    """Board rows, expanded to the NVS keys apply_stored_board_settings reads.

    Same brace-depth walk as table_keys(), but each row's scope is read from the
    row body so a scoped key expands to key+index across its whole scope.
    """
    match = decl.search(source)
    if match is None:
        return set()
    keys = set()
    for row in split_rows(source[match.end():]):
        key_match = ROW_KEY.search(row)
        if not key_match or any(v in row for v in VOLATILE_ROWS):
            continue
        key = key_match.group(1)
        scope_match = SCOPE_IN_ROW.search(row)
        scope = scope_match.group(1) if scope_match else "Global"
        if scope == "Global":
            keys.add(key)
        else:
            keys.update(f"{key}{i}" for i in range(scope_size(source, scope)))
    return keys


def board_keys() -> set:
    keys = set()
    for path in sorted(BOARD_DIR.glob("hw_*.h")):
        keys |= scoped_rows(path.read_text(), BOARD_DECL)
    return keys


def all_table_keys() -> set:
    central = TABLE.read_text()
    keys = set()
    for decl in TABLE_DECLS:
        keys |= table_keys(central, decl)
    for directory in DRIVER_DIRS:
        for path in sorted((SRC / directory).glob("*.cpp")):
            keys |= table_keys(path.read_text(), DRIVER_DECL)
    return keys


def main() -> int:
    board = board_keys()
    # Both sides: the board declares them and the generic boot pass reads them.
    boot = set(BOOT_READ.findall(NVM.read_text())) | board
    table = all_table_keys() | board

    unowned = sorted(boot - table - set(OWNED_ELSEWHERE))
    unread = sorted(table - boot - set(NOT_READ_AT_BOOT))
    stale_owned = sorted(set(OWNED_ELSEWHERE) - boot)
    stale_unread = sorted(set(NOT_READ_AT_BOOT) - table)
    overlong = sorted(k for k in table if len(k) > NVS_KEY_MAX)

    print(f"{len(boot)} keys read at boot, {len(table)} in the settings tables, "
          f"{len(boot & table)} in both.")

    if unowned:
        print("\nRead at boot but settable nowhere:")
        for k in unowned:
            print(f"    {k}")
    if unread:
        print("\nIn the settings table but never read at boot:")
        for k in unread:
            print(f"    {k}")
    if stale_owned:
        print(f"\nListed as owned elsewhere but no longer read at boot: {stale_owned}")
    if stale_unread:
        print(f"\nListed as not-read-at-boot but no longer in the table: {stale_unread}")
    if overlong:
        print(f"\nNVS keys over {NVS_KEY_MAX} characters: {overlong}")

    failures = len(unowned) + len(unread) + len(stale_owned) + len(stale_unread) + len(overlong)
    print(f"\n{failures} unexpected.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
