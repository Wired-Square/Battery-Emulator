"""Stdlib-only tests: python3 -m unittest discover -s scripts -p 'test_*.py'"""
import json
import re
import unittest
from pathlib import Path

import gen_i18n_en as g

REPO = Path(__file__).resolve().parent.parent


class EventMessageTests(unittest.TestCase):
    def test_strips_the_event_prefix(self):
        source = 'String get_event_message_string(x) {\n  switch (x) {\n' \
                 '    case EVENT_TASK_OVERRUN:\n      return "Took too long";\n  }\n}\n'
        self.assertEqual(g.event_messages(source), {"event.TASK_OVERRUN": "Took too long"})

    def test_fallthrough_cases_each_get_a_key(self):
        source = 'String get_event_message_string(x) {\n  switch (x) {\n' \
                 '    case EVENT_A:\n    case EVENT_B:\n      return "Shared";\n  }\n}\n'
        self.assertEqual(g.event_messages(source), {"event.A": "Shared", "event.B": "Shared"})

    def test_adjacent_literals_are_joined(self):
        source = ('String get_event_message_string(x) {\n  switch (x) {\n'
                  '    case EVENT_A:\n      return "first half "\n             "second half";\n'
                  '    case EVENT_B:\n      return "unrelated";\n  }\n}\n')
        self.assertEqual(g.event_messages(source),
                         {"event.A": "first half second half", "event.B": "unrelated"})

    def test_escaped_quotes_survive(self):
        source = 'String get_event_message_string(x) {\n  switch (x) {\n' \
                 '    case EVENT_A:\n      return "He said \\"go\\"";\n  }\n}\n'
        self.assertEqual(g.event_messages(source)["event.A"], 'He said "go"')


class SettingLabelTests(unittest.TestCase):
    def test_reads_the_labels_block_only(self):
        source = "const OTHER = {\n  X: 'not a label',\n};\nconst LABELS = {\n  SSID: 'SSID',\n" \
                 "  WEBUI: 'Web interface style',\n};\nconst MORE = {\n  Y: 'nor this',\n};\n"
        self.assertEqual(g.setting_labels(source),
                         {"setting.SSID": "SSID", "setting.WEBUI": "Web interface style"})


class RowLabelTests(unittest.TestCase):
    def test_pairs_key_with_label(self):
        source = "{ key: 'row.soc', label: 'SOC', value: pct(x) },"
        self.assertEqual(g.row_labels(source), {"row.soc": "SOC"})

    def test_placeholder_labels_survive(self):
        source = "{ key: 'row.battery_protocol', label: 'Battery {} protocol', arg: 1 }"
        self.assertEqual(g.row_labels(source), {"row.battery_protocol": "Battery {} protocol"})


class ApplyErrorTests(unittest.TestCase):
    def test_plain_literal(self):
        source = '    result.error = "No good.";\n    result.error_key = "error.no_good";\n'
        self.assertEqual(g.apply_errors(source), {"error.no_good": "No good."})

    def test_trailing_interpolation_becomes_a_placeholder(self):
        source = '      result.error = String("Invalid type for setting ") + field.json_key;\n' \
                 '      result.error_key = "error.setting_invalid_type";\n'
        self.assertEqual(g.apply_errors(source),
                         {"error.setting_invalid_type": "Invalid type for setting {}"})

    def test_infix_interpolation_becomes_a_placeholder(self):
        source = '      result.error = String("Setting ") + field.json_key + " is out of range";\n' \
                 '      result.error_key = "error.setting_out_of_range";\n'
        self.assertEqual(g.apply_errors(source),
                         {"error.setting_out_of_range": "Setting {} is out of range"})

    def test_parenthesised_interpolation_yields_one_placeholder(self):
        source = '        result.error = String("Battery ") + (slot + 1) + " cannot run here";\n' \
                 '        result.error_key = "error.battery_type_unsupported";\n'
        self.assertEqual(g.apply_errors(source),
                         {"error.battery_type_unsupported": "Battery {} cannot run here"})


class TranslateCallTests(unittest.TestCase):
    def test_literal_key_and_fallback(self):
        self.assertEqual(g.translate_calls("el('span', null, t('ui.on', 'On'))"), {"ui.on": "On"})

    def test_placeholder_form(self):
        self.assertEqual(g.translate_calls("tf('ui.battery_n', 'Battery {}', n)"),
                         {"ui.battery_n": "Battery {}"})

    def test_concatenated_inline_fallback_is_joined(self):
        source = "t('ui.note',\n  'first half '\n  + 'second half');"
        self.assertEqual(g.translate_calls(source), {"ui.note": "first half second half"})

    def test_module_const_fallback_is_resolved(self):
        source = "const NOTE = 'Take care.';\nel('div', null, t('ui.note', NOTE));"
        self.assertEqual(g.translate_calls(source), {"ui.note": "Take care."})

    def test_concatenated_module_const_is_joined(self):
        source = "const NOTE =\n  'first half '\n  + 'second half';\nt('ui.note', NOTE);"
        self.assertEqual(g.translate_calls(source), {"ui.note": "first half second half"})

    def test_ignores_a_fallback_it_cannot_resolve(self):
        self.assertEqual(g.translate_calls("t('ui.x', someVariable)"), {})

    def test_does_not_match_an_unrelated_identifier_ending_in_t(self):
        self.assertEqual(g.translate_calls("const at = format('ui.x', 'y');"), {})


class ShellMarkerTests(unittest.TestCase):
    def test_element_text(self):
        self.assertEqual(g.shell_markers('<a href="#/" data-i18n="ui.dashboard">Dashboard</a>'),
                         {"ui.dashboard": "Dashboard"})

    def test_attribute_marker(self):
        self.assertEqual(g.shell_markers('<nav aria-label="Sections" data-i18n-attr="aria-label:ui.sections">'),
                         {"ui.sections": "Sections"})


class RepositoryPackTests(unittest.TestCase):
    def setUp(self):
        self.strings = g.collect(REPO)

    def test_committed_pack_matches_the_sources(self):
        on_disk = json.loads((REPO / g.OUT_FILE).read_text(encoding=g.SOURCE_ENCODING))
        self.assertEqual(on_disk, self.strings, "run scripts/gen_i18n_en.py and commit " + g.OUT_FILE)

    def test_every_marked_driver_label_reaches_the_pack(self):
        marked = set()
        sources = [p for p in sorted((REPO / g.DRIVER_DIR).rglob("*")) if p.suffix in (".h", ".cpp")]
        sources.append(REPO / g.ADVANCED_API)
        for path in sources:
            marked |= set(g.TL_CALL.findall(path.read_text(encoding=g.SOURCE_ENCODING)))
        self.assertTrue(marked, "no TL() labels found; did the marker change?")
        missing = {t for t in marked
                   if g.ADVANCED_KEY_PREFIX + g.advanced_slug(
                       "".join(g.unquote_c(p) for p in g.C_STRING.findall(t))) not in self.strings}
        self.assertEqual(missing, set(), "TL() labels the advanced page cannot translate")

    def test_client_slug_matches_the_generator(self):
        source = (REPO / "Software/src/devboard/webserver/web/advanced.js").read_text(encoding=g.SOURCE_ENCODING)
        client = re.search(r"function advText\(text\) \{(.*?)\n\}", source, re.S)
        self.assertIsNotNone(client, "advText() moved; the slug rules can now drift apart")
        body = client.group(1)
        for pattern in (g.SLUG_STRIP.pattern, g.SLUG_RUNS.pattern):
            self.assertIn(pattern, body,
                          "advanced.js slugs labels differently from advanced_slug(), so keys will miss")

    def test_every_event_type_has_a_message(self):
        header = (REPO / "Software/src/devboard/utils/events.h").read_text(encoding=g.SOURCE_ENCODING)
        declared = set()
        for line in header.splitlines():
            if "XX(EVENT_" not in line or "EVENT_NOF_EVENTS" in line:
                continue
            name = line.split("XX(", 1)[1].split(")", 1)[0]
            if not name.startswith("EVENT_LEVEL_"):
                declared.add("event." + name[len(g.EVENT_PREFIX):])
        keyed = {k for k in self.strings if k.startswith(g.EVENT_KEY_PREFIX)}
        self.assertEqual(declared - keyed, set(), "event types the SPA can receive but cannot translate")
        self.assertEqual(keyed - declared, set(), "translations for event types that no longer exist")

    def test_every_returned_event_message_reaches_the_pack(self):
        source = (REPO / "Software/src/devboard/utils/events.cpp").read_text(encoding=g.SOURCE_ENCODING)
        body = g.block_after(source, g.MESSAGE_FN, g.CPP_BLOCK_END)
        returned = set()
        for match in re.finditer(r'return\s+((?:"(?:[^"\\]|\\.)*"\s*)+);', body):
            text = "".join(g.unquote_c(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', match.group(1)))
            if text:
                returned.add(text)
        published = {v for k, v in self.strings.items() if k.startswith(g.EVENT_KEY_PREFIX)}
        self.assertEqual(returned - published, set(),
                         "a message the firmware returns never reached the pack, so some key carries another event's text")

    def test_carries_no_proper_noun_namespaces(self):
        reserved = ("battery.", "inverter.", "charger.", "shunt.", "interface.", "lang.")
        offenders = [k for k in self.strings if k.startswith(reserved)]
        self.assertEqual(offenders, [],
                         "product names and language endonyms must not be translated")

    def test_no_string_is_empty(self):
        self.assertEqual([k for k, v in self.strings.items() if not v.strip()], [])

    def test_no_string_has_edge_whitespace(self):
        ragged = [k for k, v in self.strings.items() if v != v.strip()]
        self.assertEqual(ragged, [], "a truncated concatenation would leave a trailing space")

    def test_no_sentence_like_literal_escapes_the_pack(self):
        js_string = r"'(?:[^'\\]|\\.)*'"
        translated = re.compile(r"\btf?\(\s*'(?:[^'\\]|\\.)*'\s*,\s*((?:%s)(?:\s*\+\s*(?:%s))*)"
                                % (js_string, js_string))
        labelled = re.compile(r"label:\s*(%s)" % js_string)
        offenders = []
        for path in sorted((REPO / g.WEB_DIR).glob("*.js")):
            source = path.read_text(encoding=g.SOURCE_ENCODING)
            consts = g.module_string_consts(source)
            covered = set()
            for call in translated.finditer(source):
                for literal in re.finditer(js_string, call.group(1)):
                    covered.add(call.start(1) + literal.start())
            for label in labelled.finditer(source):
                covered.add(label.start(1))
            for match in re.finditer(js_string, source):
                if match.start() in covered:
                    continue
                value = match.group(0)[1:-1]
                if not value or " " not in value or not value[0].isupper():
                    continue
                if any(value in resolved for resolved in consts.values()):
                    continue
                offenders.append("%s:%d %r" % (path.name, source[:match.start()].count("\n") + 1, value))
        self.assertEqual(offenders, [], "sentence-like text that never reaches a translator")

    def test_no_bare_display_literal_survives(self):
        js_string = r"'(?:[^'\\]|\\.)*'"
        patterns = (
            re.compile(r"\bel\(\s*(?:%s|null)\s*,\s*(?:%s|`[^`]*`|null)\s*,\s*(%s)"
                       % (js_string, js_string, js_string)),
            re.compile(r"\bwindow\.(?:alert|confirm|prompt)\(\s*(%s)" % js_string),
            re.compile(r"\bel\((?:[^()]*?),\s*(`[^`]*\$\{[^`]*`)\s*\)"),
            re.compile(r"\bwindow\.(?:alert|confirm|prompt)\(\s*(`[^`]*\$\{[^`]*`)"),
            re.compile(r"\.(?:textContent|title)\s*=\s*(`[^`]*\$\{[^`]*`)"),
            re.compile(r"\.textContent\s*=\s*(%s)" % js_string),
        )
        # A template whose fixed text is only unit symbols or hardware identifiers
        # (" mA", "CH", "SW") is not prose; prose needs a word of three letters or more.
        prose = re.compile(r"[A-Za-z]{3}")
        offenders = []
        for path in sorted((REPO / g.WEB_DIR).glob("*.js")):
            source = path.read_text(encoding=g.SOURCE_ENCODING)
            for pattern in patterns:
                for match in pattern.finditer(source):
                    literal = match.group(1)
                    fixed = re.sub(r"\$\{[^}]*\}", "", literal[1:-1])
                    if fixed.strip() and prose.search(fixed):
                        offenders.append("%s: %s" % (path.name, literal))
        self.assertEqual(offenders, [], "display text must go through t() so it reaches the pack")

    def test_every_shell_marker_resolves(self):
        for path in sorted((REPO / g.WEB_DIR).glob("shell-*.html")):
            source = path.read_text(encoding=g.SOURCE_ENCODING)
            for key in re.findall(r'data-i18n="([^"]+)"', source):
                self.assertIn(key, self.strings, "%s marks %s, which is not in the pack" % (path.name, key))
            for key in re.findall(r'data-i18n-attr="[^":]+:([^"]+)"', source):
                self.assertIn(key, self.strings, "%s marks %s, which is not in the pack" % (path.name, key))

    def test_every_literal_translate_key_reaches_the_pack(self):
        for path in sorted((REPO / g.WEB_DIR).glob("*.js")):
            source = path.read_text(encoding=g.SOURCE_ENCODING)
            for key, _ in g.TRANSLATE_CALL.findall(source):
                self.assertIn(g.unquote_js(key), self.strings,
                              "%s calls %s, whose English never reached the pack" % (path.name, key))


if __name__ == "__main__":
    unittest.main()
