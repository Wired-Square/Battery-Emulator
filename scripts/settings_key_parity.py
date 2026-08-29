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
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "Software/src"
NVM = SRC / "communication/nvm/comm_nvm.cpp"
TABLE = SRC / "devboard/webserver/settings_api.cpp"
DRIVER_DIRS = ("battery", "inverter", "charger")

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
TABLE_DECLS = ("const SettingField kSettingFields[] = {", "const DeviceSetting kFamilySettingFields[] = {")
DRIVER_DECL = "const DeviceSetting kSettings[] = {"
ROW_KEY = re.compile(r'^\s*\{\{?"([A-Za-z0-9_]+)",\s*(?:ST|SettingType)::', re.M)
VOLATILE_ROWS = ("SS::Volatile", "SettingStorage::Volatile")
NVS_KEY_MAX = 15


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
    keys = set()
    depth = 0
    row = ""
    for char in body:
        if depth == 0 and char == "}":
            break
        row += char
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                match = ROW_KEY.search(row)
                if match and not any(v in row for v in VOLATILE_ROWS):
                    keys.add(match.group(1))
                row = ""
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
    boot = set(BOOT_READ.findall(NVM.read_text()))
    table = all_table_keys()

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
