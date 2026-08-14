#!/usr/bin/env python3
"""Build a clangd compilation database for the firmware from a verbose PlatformIO build.

`pio run -t compiledb` emits only framework and managed-component translation units — never
the project's own Software/src sources — so clangd is left inferring project flags from a
framework entry and reports phantom errors. The verbose build does print every real command,
so that is what this parses.

    python3 scripts/gen_compile_db.py            # default env
    python3 scripts/gen_compile_db.py -e stark_330

Writes compile_commands.json in the project root. That file is deliberately not tracked;
see .git/info/exclude. Stdlib only, to match the other scripts.
"""
import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

DEFAULT_ENV = "wiredflexlink"
PIO = Path.home() / ".platformio" / "penv" / "bin" / "pio"
PROJECT_ROOT = Path(__file__).resolve().parent.parent
# A compile line ends in the source path and carries -c; the linker line has neither.
COMPILE_RE = re.compile(r"^(?P<cc>\S*(?:gcc|g\+\+))\s+-o\s+(?P<obj>\S+\.o)\s+-c\s+(?P<rest>.*)$")


def parse(log: str) -> list[dict]:
    entries, seen = [], set()
    for line in log.splitlines():
        m = COMPILE_RE.match(line.strip())
        if not m:
            continue
        try:
            args = shlex.split(m.group("rest"))
        except ValueError:
            continue
        sources = [a for a in args if a.endswith((".c", ".cpp", ".cc", ".S", ".s"))]
        if not sources:
            continue
        source = sources[-1]
        path = (PROJECT_ROOT / source).resolve()
        # Framework and package sources are already covered by PlatformIO's own database;
        # this exists for the project's sources, which that database omits.
        if not path.is_file() or PROJECT_ROOT not in path.parents:
            continue
        if str(path) in seen:
            continue
        seen.add(str(path))
        entries.append(
            {"directory": str(PROJECT_ROOT), "file": str(path), "command": line.strip()}
        )
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-e", "--environment", default=DEFAULT_ENV)
    ap.add_argument("--log", type=Path, help="parse a saved verbose build log instead of building")
    args = ap.parse_args()

    if args.log:
        log = args.log.read_text(errors="replace")
    else:
        # Only freshly compiled units print a command, so start from a clean build.
        subprocess.run([str(PIO), "run", "-t", "clean", "-e", args.environment],
                       cwd=PROJECT_ROOT, capture_output=True)
        proc = subprocess.run([str(PIO), "run", "-v", "-e", args.environment],
                              cwd=PROJECT_ROOT, capture_output=True, text=True)
        log = proc.stdout + proc.stderr
        if proc.returncode != 0:
            print(f"build failed for {args.environment}; database not written", file=sys.stderr)
            return 1

    entries = parse(log)
    if not entries:
        print("no project compile commands found", file=sys.stderr)
        return 1

    out = PROJECT_ROOT / "compile_commands.json"
    out.write_text(json.dumps(entries, indent=1))
    print(f"{len(entries)} project entries -> {out.relative_to(PROJECT_ROOT)} ({args.environment})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
