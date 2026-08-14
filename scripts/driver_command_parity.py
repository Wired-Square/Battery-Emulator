#!/usr/bin/env python3
"""Per-driver command-id parity: declared lists now vs what the old predicates produced.

Baseline is the pre-Task-14 commit. The registry gated each id on a capability
predicate; that mapping is transcribed below from advanced_api.cpp at the baseline.
Capabilities that gated nothing, and ids no driver could reach, both show up as
differences rather than being silently normalised away.

Two expected differences are ids the baseline declared a capability for but never
bound in the registry, leaving them unreachable from the web UI: TESLA-BATTERY.h
gaining startBalancing/endBalancing, and TEST-FAKE-BATTERY.h gaining setFakeVoltage.
Later entries are commands the drivers genuinely gained after the baseline by
adopting upstream changes (the upstream PR is noted per entry).
"""
import re
import subprocess
import sys
from pathlib import Path

BASELINE = "0be9b9a7"
REPO = Path(__file__).resolve().parent.parent
BATT = REPO / "Software/src/battery"

# Headers that hold no driver: shared descriptors and the abstract base.
NON_DRIVER_HEADERS = ("Battery.h", "battery_command.h", "UdsCanBattery.h")

# driver -> (ids lost, ids gained) that are intended. Anything else fails the run,
# so this stays re-runnable as a gate rather than always reporting the known delta.
EXPECTED_DIFFS = {
    "TESLA-BATTERY.h": (frozenset(), frozenset({"startBalancing", "endBalancing"})),
    "TEST-FAKE-BATTERY.h": (frozenset(), frozenset({"setFakeVoltage"})),
    # Upstream #2681: Nissan Leaf gained UDS DTC readout.
    "NISSAN-LEAF-BATTERY.h": (frozenset(), frozenset({"readDTC"})),
    # Upstream #2690: Ford Mach-E gained UDS DTC readout.
    "FORD-MACH-E-BATTERY.h": (frozenset(), frozenset({"readDTC"})),
    # Upstream #1941: MG-GEN1 (replacing MG-HS-PHEV) gained a BMS reset command.
    "MG-GEN1-BATTERY.h": (frozenset(), frozenset({"resetBMS"})),
    # Upstream #2656: BYD Atto 3 gained isolation monitor controls.
    "BYD-ATTO-3-BATTERY.h": (frozenset(), frozenset({"isoMonitorEnable", "isoMonitorDisable"})),
    # Upstream #2730: MEB gained a crash reset command.
    "MEB-BATTERY.h": (frozenset(), frozenset({"resetCrash"})),
}

# capability -> ids it made reachable, transcribed from the baseline registry.
CAP_TO_IDS = {
    "supports_clear_isolation": ["clearIsolation"],
    "supports_calibrate_SOC": ["calibrateSOC"],
    "supports_chademo_restart": ["chademoRestart", "chademoStop"],
    "supports_reset_BMS": ["resetBMS"],
    "supports_reset_SOC": ["resetSOC"],
    "supports_reset_crash": ["resetCrash"],
    "supports_reset_NVROL": ["resetNVROL"],
    "supports_contactor_reset": ["resetContactor"],
    "supports_reset_DTC": ["resetDTC"],
    "supports_balancing": ["startBalancing", "endBalancing"],
    "supports_balancing_request": ["startBalancingRequest", "stopBalancingRequest"],
    "supports_isolation_test": ["isolationTest"],
    "supports_read_DTC": ["readDTC"],
    "supports_reset_BECM": ["resetBECM"],
    "supports_contactor_close": ["contactorClose", "contactorOpen"],
    "supports_reset_SOH": ["resetSOH"],
    "supports_factory_mode_method": ["setFactoryMode"],
    "supports_toggle_SOC_method": ["toggleSOC"],
    "supports_energy_saving_mode_reset": ["resetEnergySavingMode"],
    # Declared by drivers but bound to no registry entry, so reachable from nothing.
    "supports_chademo_stop": [],
    "supports_manual_balancing": [],
    "supports_set_fake_voltage": [],
    # Genuine capability queries, never commands.
    "supports_charged_energy": [],
    "supports_real_BMS_status": [],
}


def descriptor_ids():
    """CMD_* constant -> its identifier string."""
    src = (BATT / "battery_command.h").read_text()
    return dict(re.findall(r'Battery(?:Value)?CommandDescriptor\s+(CMD_\w+)\s*\{\s*\{?\s*"([^"]+)"', src))


def baseline_expected():
    listing = subprocess.run(
        ["git", "-C", str(REPO), "ls-tree", "--name-only", f"{BASELINE}:Software/src/battery"],
        capture_output=True, text=True, check=True).stdout.split()
    expected, unknown = {}, set()
    for name in listing:
        if not name.endswith(".h") or name in NON_DRIVER_HEADERS:
            continue
        src = subprocess.run(
            ["git", "-C", str(REPO), "show", f"{BASELINE}:Software/src/battery/{name}"],
            capture_output=True, text=True, check=True).stdout
        caps = re.findall(r"^\s*(?:virtual\s+)?bool\s+(supports_\w+)\(\)", src, re.M)
        if not caps:
            continue
        ids = set()
        for cap in caps:
            if cap not in CAP_TO_IDS:
                unknown.add(cap)
                continue
            ids.update(CAP_TO_IDS[cap])
        expected[name] = ids
    return expected, unknown


def declared_now(cmd_ids):
    out, unknown = {}, set()
    for path in sorted(BATT.glob("*.h")):
        if path.name in NON_DRIVER_HEADERS:
            continue
        src = path.read_text()
        consts = re.findall(r"\b(?:value_)?command\(\s*(CMD_\w+)", src)
        if not consts:
            continue
        ids = set()
        for c in consts:
            if c not in cmd_ids:
                unknown.add(c)
                continue
            ids.add(cmd_ids[c])
        out[path.name] = ids
    return out, unknown


def main():
    cmd_ids = descriptor_ids()
    expected, unknown_caps = baseline_expected()
    actual, unknown_consts = declared_now(cmd_ids)

    if unknown_caps:
        print(f"UNMAPPED capabilities (fix the table): {sorted(unknown_caps)}")
    if unknown_consts:
        print(f"UNKNOWN CMD_ constants: {sorted(unknown_consts)}")

    drivers = sorted(set(expected) | set(actual))
    diffs = unexpected = 0
    differing = set()
    for d in drivers:
        exp, act = expected.get(d, set()), actual.get(d, set())
        if exp == act:
            continue
        diffs += 1
        differing.add(d)
        lost, gained = exp - act, act - exp
        known = EXPECTED_DIFFS.get(d) == (frozenset(lost), frozenset(gained))
        if not known:
            unexpected += 1
        print(f"\n{d}{'  (expected)' if known else ''}")
        if lost:
            print(f"    LOST : {sorted(lost)}")
        if gained:
            print(f"    GAINED: {sorted(gained)}")

    # An entry is only consulted when a difference exists, so a driver drifting back
    # to its baseline set would go quiet rather than fail.
    vanished = sorted(set(EXPECTED_DIFFS) - differing)
    if vanished:
        unexpected += len(vanished)
        print(f"\nEXPECTED difference gone, command dropped? {vanished}")

    print(f"\n{len(drivers)} drivers compared, {diffs} with differences, {unexpected} unexpected.")
    unconverted = sorted(d for d in expected if expected[d] and d not in actual)
    if unconverted:
        print(f"Still on the legacy path ({len(unconverted)}): {unconverted}")
        unexpected += len(unconverted)
    return 0 if unexpected == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
