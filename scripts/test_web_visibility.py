"""Behavioural tests for the settings visibility rules.

The rules decide whether a control is reachable at all, so a mistake hides a
setting rather than failing loudly. `node --check` only parses the module, so
these drive the real exported functions with fabricated schema and state.
"""
import json
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MODULE = REPO / "Software/src/devboard/webserver/web/visibility.js"
NODE = "node"

# Mirrors of the firmware enums, pinned deliberately: the point of these tests is
# to fail when the emitted ownership stops matching the behaviour it replaced.
TESLA_3Y, TESLA_SX, BMW_PHEV, BYD, SOFAR = 32, 33, 43, 5, 17

SCHEMA = [
    {"key": "max_cell_mv", "domain": "battery", "owners": [TESLA_3Y, TESLA_SX]},
    {"key": "max_time_min", "domain": "battery", "owners": [TESLA_3Y, TESLA_SX, BMW_PHEV]},
    {"key": "GTWPACK", "domain": "battery", "owners": [TESLA_3Y, TESLA_SX]},
    {"key": "BYDAUTOCALEN", "domain": "battery", "owners": [BYD], "slot": 0},
    {"key": "BYDAUTOCALEN2", "domain": "battery", "owners": [BYD], "slot": 1},
    {"key": "SOFAR_ID", "domain": "inverter", "owners": [SOFAR]},
    {"key": "HOSTNAME"},
]

DRIVER = """
import {{ isVisible }} from '{module}';
const schema = {schema};
const out = {cases}.map(([key, types, extra]) => {{
  const ctx = {{
    schema,
    state: Object.assign({{ GTWCHASSIS: 0, INVTYPE: 0 }}, extra || {{}}),
    dynamicState: {{ batteries: types.map((t, i) => ({{ slot: i, type: t }})) }},
  }};
  return isVisible(key, ctx);
}});
console.log(JSON.stringify(out));
"""


def evaluate(cases):
    script = DRIVER.format(module=MODULE.as_uri(), schema=json.dumps(SCHEMA), cases=json.dumps(cases))
    result = subprocess.run([NODE, "--input-type=module", "-e", script],
                            capture_output=True, text=True, cwd=REPO)
    if result.returncode != 0:
        raise AssertionError(result.stderr.strip())
    return json.loads(result.stdout)


class VisibilityRules(unittest.TestCase):
    def assert_cases(self, cases_with_expected):
        cases = [c[:3] for c in cases_with_expected]
        names = [c[3] for c in cases_with_expected]
        expected = [c[4] for c in cases_with_expected]
        got = evaluate(cases)
        for name, want, have in zip(names, expected, got):
            self.assertEqual(have, want, f"{name}: expected {'shown' if want else 'hidden'}")

    def test_balancing_time_follows_every_owning_driver(self):
        self.assert_cases([
            ("max_time_min", [BMW_PHEV], {}, "BMW PHEV alone", True),
            ("max_time_min", [TESLA_3Y], {"GTWCHASSIS": 2}, "Tesla 3/Y", True),
            ("max_time_min", [TESLA_3Y], {"GTWCHASSIS": 0}, "Tesla below 3/Y", False),
            ("max_time_min", [BMW_PHEV, TESLA_3Y], {"GTWCHASSIS": 0},
             "BMW PHEV beside a sub-3/Y Tesla", True),
            ("max_time_min", [BMW_PHEV, TESLA_3Y], {"GTWCHASSIS": 2}, "BMW PHEV beside a 3/Y", True),
            ("max_time_min", [0], {}, "no battery", False),
        ])

    def test_chassis_gated_rows_need_a_qualifying_tesla(self):
        self.assert_cases([
            ("max_cell_mv", [TESLA_3Y], {"GTWCHASSIS": 2}, "Tesla 3/Y", True),
            ("max_cell_mv", [TESLA_3Y], {"GTWCHASSIS": 0}, "Tesla below 3/Y", False),
            ("max_cell_mv", [BMW_PHEV], {"GTWCHASSIS": 2}, "BMW PHEV only", False),
        ])

    def test_rows_follow_the_slot_that_owns_them(self):
        self.assert_cases([
            ("BYDAUTOCALEN", [BYD], {}, "BYD in slot 1", True),
            ("BYDAUTOCALEN2", [BYD], {}, "second BYD row, slot 2 empty", False),
            ("BYDAUTOCALEN2", [BYD, BYD], {}, "second BYD row, slot 2 occupied", True),
            ("BYDAUTOCALEN", [TESLA_3Y], {}, "BYD row on a Tesla install", False),
        ])

    def test_owner_matching_spans_every_slot_not_just_the_primary(self):
        self.assert_cases([
            ("GTWPACK", [BMW_PHEV, TESLA_3Y], {}, "Tesla in the second slot", True),
            ("GTWPACK", [BMW_PHEV], {}, "no Tesla anywhere", False),
        ])

    def test_inverter_rows_follow_the_selected_protocol(self):
        self.assert_cases([
            ("SOFAR_ID", [0], {"INVTYPE": SOFAR}, "Sofar selected", True),
            ("SOFAR_ID", [0], {"INVTYPE": 10}, "another inverter", False),
        ])

    def test_rows_without_owners_are_always_applicable(self):
        self.assert_cases([("HOSTNAME", [0], {}, "row with no owners and no rule", True)])


if __name__ == "__main__":
    unittest.main()
