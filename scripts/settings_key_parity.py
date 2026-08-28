#!/usr/bin/env python3
"""NVS key ownership parity between the boot load path and the settings table.

comm_nvm.cpp reads settings straight from NVS at boot; kSettingFields describes
what the web API can read and write. A key present in one and absent from the
other is how the two drift apart, which is the class of bug upstream #2697 and
#2839 both fixed.

Not every boot key belongs in the table. Some are datalayer-backed and persisted
by comm_nvm's own store_settings(), others are written through the dynamic
sections of the settings POST, and one belongs to the MQTT discovery client.
Those owners are listed below, so anything left unowned fails the run.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "Software/src"
NVM = SRC / "communication/nvm/comm_nvm.cpp"
TABLE = SRC / "devboard/webserver/settings_api.cpp"

# Boot keys the settings table deliberately does not describe, and who owns them.
OWNED_ELSEWHERE = {
    "BATTERY_WH_MAX": "POST /api/chargelimits",
    "MAXCHARGEAMP": "POST /api/chargelimits",
    "MAXDISCHARGEAMP": "POST /api/chargelimits",
    "MAXPERCENTAGE": "POST /api/chargelimits",
    "MINPERCENTAGE": "POST /api/chargelimits",
    "TARGETCHVOLT": "POST /api/chargelimits",
    "TARGETDISCHVOLT": "POST /api/chargelimits",
    "USE_SCALED_SOC": "POST /api/chargelimits",
    "USEVOLTLIMITS": "POST /api/chargelimits",
    "BYDAUTOCALEN": "POST /api/bydautocal",
    "BYDAUTOCALEN2": "POST /api/bydautocal",
    "BYDAUTOCALDRIFT": "POST /api/bydautocal",
    "BYDAUTOCALDRFT2": "POST /api/bydautocal",
    "BYDKEEPISOOFF": "POST /api/bydautocal",
    "BMSRESETDUR": "POST /api/recoverymode",
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
    "RAMPDOWNSOC": "read by the estimated-SOC battery driver",
}

BOOT_READ = re.compile(r'settings\.get[A-Za-z]+\("([A-Za-z0-9_]+)"')
TABLE_ROW = re.compile(r'^\s*\{"([A-Za-z0-9_]+)",\s*ST::', re.M)
NVS_KEY_MAX = 15


def main() -> int:
    boot = set(BOOT_READ.findall(NVM.read_text()))
    table = set(TABLE_ROW.findall(TABLE.read_text()))

    unowned = sorted(boot - table - set(OWNED_ELSEWHERE))
    unread = sorted(table - boot - set(NOT_READ_AT_BOOT))
    stale_owned = sorted(set(OWNED_ELSEWHERE) - boot)
    stale_unread = sorted(set(NOT_READ_AT_BOOT) - table)
    overlong = sorted(k for k in table if len(k) > NVS_KEY_MAX)

    print(f"{len(boot)} keys read at boot, {len(table)} in kSettingFields, "
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
