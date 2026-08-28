"""Stdlib-only tests: python3 -m unittest discover -s scripts -p 'test_*.py'

The SPA has no test harness, so a syntax error would otherwise reach the device
and blank the page. Skipped where node is unavailable.
"""
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WEB_DIR = REPO / "Software/src/devboard/webserver/web"
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "node is not installed")
class ModuleSyntaxTests(unittest.TestCase):
    def test_every_module_parses(self):
        for path in sorted(WEB_DIR.glob("*.js")):
            with self.subTest(module=path.name):
                result = subprocess.run([NODE, "--input-type=module", "--check"],
                                        stdin=path.open("rb"), capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
