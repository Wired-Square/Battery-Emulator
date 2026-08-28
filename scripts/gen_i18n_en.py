"""Extract the keyed English display strings into translations/en.json.

English ships inline in the sources as every t() call's fallback, so this pack is
never served by the device: it is the source of truth translators work from, and
what populates the public translation repository.

Runs as a PlatformIO pre-build extra_script and as a plain module under test.
Stdlib only: every environment and any CI must build without pip.
"""
import json
import re
from pathlib import Path

SOURCE_ENCODING = "utf-8"

EVENTS_SOURCE = "Software/src/devboard/utils/events.cpp"
SETTINGS_JS = "Software/src/devboard/webserver/web/settings.js"
DASHBOARD_JS = "Software/src/devboard/webserver/web/dashboard.js"
SETTINGS_API = "Software/src/devboard/webserver/settings_api.cpp"
DRIVER_DIR = "Software/src/battery"
ADVANCED_API = "Software/src/devboard/webserver/advanced_api.cpp"
WEB_DIR = "Software/src/devboard/webserver/web"
OUT_FILE = "translations/en.json"

EVENT_PREFIX = "EVENT_"
EVENT_KEY_PREFIX = "event."
SETTING_KEY_PREFIX = "setting."
ADVANCED_KEY_PREFIX = "adv."

MESSAGE_FN = "String get_event_message_string"
LABELS_DECL = "const LABELS = {"
JS_BLOCK_END = "\n};"
CPP_BLOCK_END = "\n}\n"

EVENT_CASE = re.compile(r'case (EVENT_\w+):|return\s+((?:"(?:[^"\\]|\\.)*"\s*)+);')
LABEL_ENTRY = re.compile(r"^\s{2}(\w+): *('(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\")\s*,\s*$", re.M)
ROW_ENTRY = re.compile(r"key: '([^']+)',\s*label: '((?:[^'\\]|\\.)*)'")
ERROR_ENTRY = re.compile(r'result\.error = ([^;]+);\s*\n\s*result\.error_key = "([^"]+)";')
C_STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
EXPRESSION_NOISE = re.compile(r'[\s+()]|String')
ERROR_ARG_PLACEHOLDER = "{}"
JS_STRING = r"'(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\""
TRANSLATE_CALL = re.compile(r"\btf?\(\s*('(?:[^'\\]|\\.)*')\s*,\s*("
                            r"(?:" + JS_STRING + r")(?:\s*\+\s*(?:" + JS_STRING + r"))*"
                            r"|[A-Z][A-Z0-9_]*)")
MODULE_CONST = re.compile(r"^const ([A-Z][A-Z0-9_]*) =\s*((?:" + JS_STRING + r")(?:\s*\+\s*(?:" + JS_STRING + r"))*)\s*;",
                          re.M | re.S)
SHELL_MARKER = re.compile(r'data-i18n="([^"]+)"[^>]*>([^<]*)<')
SHELL_ATTR_MARKER = re.compile(r'(\w[\w-]*)="([^"]*)"[^>]*data-i18n-attr="\1:([^"]+)"')
# TL() marks a driver label or section title as English prose rather than a
# signal name. The slug must match slugify() in advanced.js.
TL_CALL = re.compile(r'\bTL\(\s*((?:"(?:[^"\\\\]|\\\\.)*"\s*)+)\)')
SLUG_STRIP = re.compile(r"[^a-z0-9]+")
SLUG_RUNS = re.compile(r"_+")


def unquote_js(literal: str) -> str:
    body = literal[1:-1]
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), body)


def unquote_c(literal: str) -> str:
    return unquote_js(literal)


def block_after(text: str, marker: str, end: str) -> str:
    start = text.index(marker)
    return text[start:text.index(end, start)]


def event_messages(source: str) -> dict:
    body = block_after(source, MESSAGE_FN, CPP_BLOCK_END)
    out = {}
    pending = []
    for match in EVENT_CASE.finditer(body):
        name, literal = match.group(1), match.group(2)
        if name is not None:
            pending.append(name)
            continue
        message = "".join(unquote_c(part) for part in C_STRING.findall(literal))
        for event in pending:
            out[EVENT_KEY_PREFIX + event[len(EVENT_PREFIX):]] = message
        pending = []
    return out


def setting_labels(source: str) -> dict:
    body = block_after(source, LABELS_DECL, JS_BLOCK_END)
    return {SETTING_KEY_PREFIX + key: unquote_js(literal) for key, literal in LABEL_ENTRY.findall(body)}


def row_labels(source: str) -> dict:
    return {key: unquote_js("'%s'" % label) for key, label in ROW_ENTRY.findall(source)}


def apply_errors(source: str) -> dict:
    out = {}
    for expression, key in ERROR_ENTRY.findall(source):
        parts = []
        cursor = 0
        for literal in C_STRING.finditer(expression):
            if EXPRESSION_NOISE.sub("", expression[cursor:literal.start()]):
                parts.append(ERROR_ARG_PLACEHOLDER)
            parts.append(unquote_c(literal.group(0)))
            cursor = literal.end()
        if EXPRESSION_NOISE.sub("", expression[cursor:]):
            parts.append(ERROR_ARG_PLACEHOLDER)
        out[key] = "".join(parts)
    return out


def module_string_consts(source: str) -> dict:
    out = {}
    for name, expression in MODULE_CONST.findall(source):
        out[name] = "".join(unquote_js(part) for part in re.findall(JS_STRING, expression))
    return out


def translate_calls(source: str) -> dict:
    consts = module_string_consts(source)
    out = {}
    for key, fallback in TRANSLATE_CALL.findall(source):
        fallback = fallback.strip()
        if fallback[0] in "'\"":
            out[unquote_js(key)] = "".join(unquote_js(part) for part in re.findall(JS_STRING, fallback))
        elif fallback in consts:
            out[unquote_js(key)] = consts[fallback]
    return out


def advanced_slug(text: str) -> str:
    return SLUG_RUNS.sub("_", SLUG_STRIP.sub("_", text.lower())).strip("_")


def advanced_labels(source: str) -> dict:
    out = {}
    for literals in TL_CALL.findall(source):
        text = "".join(unquote_c(part) for part in C_STRING.findall(literals))
        out[ADVANCED_KEY_PREFIX + advanced_slug(text)] = text
    return out


def shell_markers(source: str) -> dict:
    out = {key: text for key, text in SHELL_MARKER.findall(source)}
    for _, value, key in SHELL_ATTR_MARKER.findall(source):
        out[key] = value
    return out


def collect(repo_root: Path) -> dict:
    root = Path(repo_root)
    strings = {}
    strings.update(event_messages(root.joinpath(EVENTS_SOURCE).read_text(encoding=SOURCE_ENCODING)))
    strings.update(setting_labels(root.joinpath(SETTINGS_JS).read_text(encoding=SOURCE_ENCODING)))
    strings.update(row_labels(root.joinpath(DASHBOARD_JS).read_text(encoding=SOURCE_ENCODING)))
    strings.update(apply_errors(root.joinpath(SETTINGS_API).read_text(encoding=SOURCE_ENCODING)))
    marked = [p for p in sorted((root / DRIVER_DIR).rglob("*")) if p.suffix in (".h", ".cpp")]
    marked.append(root / ADVANCED_API)
    for path in marked:
        strings.update(advanced_labels(path.read_text(encoding=SOURCE_ENCODING)))
    web = root / WEB_DIR
    for path in sorted(web.glob("*.js")):
        strings.update(translate_calls(path.read_text(encoding=SOURCE_ENCODING)))
    for path in sorted(web.glob("shell-*.html")):
        strings.update(shell_markers(path.read_text(encoding=SOURCE_ENCODING)))
    return dict(sorted(strings.items()))


def generate(repo_root: Path) -> dict:
    strings = collect(repo_root)
    out_path = Path(repo_root) / OUT_FILE
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(strings, ensure_ascii=False, indent=1, sort_keys=True) + "\n",
                        encoding=SOURCE_ENCODING)
    return strings


def _main(project_root: Path, quiet: bool) -> None:
    strings = generate(project_root)
    if not quiet:
        print("%s: %d strings" % (OUT_FILE, len(strings)))


if "Import" in globals():
    Import("env")  # noqa: F821
    _main(Path(env["PROJECT_DIR"]), quiet=True)  # noqa: F821
elif __name__ == "__main__":
    _main(Path(__file__).parent.parent, quiet=False)
