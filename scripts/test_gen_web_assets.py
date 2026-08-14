"""Stdlib-only tests: python3 -m unittest discover -s scripts -p 'test_*.py'"""
import gzip
import tempfile
import unittest
from pathlib import Path

import gen_web_assets as g


class ContentTypeTests(unittest.TestCase):
    def test_known_suffixes(self):
        self.assertEqual(g.content_type_for(".html"), "text/html")
        self.assertEqual(g.content_type_for(".css"), "text/css")
        self.assertEqual(g.content_type_for(".js"), "application/javascript")
        self.assertEqual(g.content_type_for(".svg"), "image/svg+xml")

    def test_unknown_suffix_is_octet_stream(self):
        self.assertEqual(g.content_type_for(".bin"), "application/octet-stream")


class EtagTests(unittest.TestCase):
    def test_is_quoted_and_stable(self):
        tag = g.etag_for(b"hello")
        self.assertTrue(tag.startswith('"') and tag.endswith('"'))
        self.assertEqual(tag, g.etag_for(b"hello"))

    def test_changes_with_content(self):
        self.assertNotEqual(g.etag_for(b"hello"), g.etag_for(b"hello!"))


class GzipTests(unittest.TestCase):
    def test_roundtrip(self):
        self.assertEqual(gzip.decompress(g.gzip_bytes(b"payload" * 50)), b"payload" * 50)

    def test_deterministic(self):
        # A timestamp in the gzip header would churn the ETag on every build.
        self.assertEqual(g.gzip_bytes(b"payload"), g.gzip_bytes(b"payload"))


class MinifyTests(unittest.TestCase):
    def test_drops_blank_lines_and_indentation(self):
        self.assertEqual(g.minify("a {\n\n  color: red;\n}\n"), "a {\ncolor: red;\n}")

    def test_drops_full_line_comments(self):
        self.assertEqual(g.minify("// note\nlet x = 1;\n"), "let x = 1;")

    def test_preserves_content_inside_strings(self):
        src = 'let s = "// not a comment";\n'
        self.assertIn('"// not a comment"', g.minify(src))

    def test_line_breaks_survive(self):
        # Joining with "" would break ASI, merge adjacent HTML words, and
        # flatten multi-line template literals. gzip makes the newline free.
        self.assertEqual(g.minify("let a = 1\nlet b = 2\n"), "let a = 1\nlet b = 2")

    def test_multiline_template_literal_keeps_its_newlines(self):
        src = 'const t = `line one\nline two`;\n'
        self.assertEqual(g.minify(src), 'const t = `line one\nline two`;')


class GenerateTests(unittest.TestCase):
    def test_emits_header_and_source_with_every_asset(self):
        with tempfile.TemporaryDirectory() as tmp:
            web, out = Path(tmp) / "web", Path(tmp) / "generated"
            web.mkdir()
            (web / "index.html").write_text("<p>hi</p>")
            (web / "app.js").write_text("let x = 1;")

            assets = g.generate(web, out)

            self.assertEqual([a.path for a in assets], ["/app.js", "/index.html"])  # sorted
            source = (out / "web_assets.cpp").read_text()
            self.assertIn('"/index.html"', source)
            self.assertIn("kWebAssetCount = 2", source)
            self.assertIn("const WebAsset kWebAssets[]", source)
            self.assertIn("struct WebAsset", (out / "web_assets.h").read_text())

    def test_is_reproducible(self):
        with tempfile.TemporaryDirectory() as tmp:
            web, out = Path(tmp) / "web", Path(tmp) / "generated"
            web.mkdir()
            (web / "app.js").write_text("let x = 1;")
            g.generate(web, out)
            first = (out / "web_assets.cpp").read_text()
            g.generate(web, out)
            self.assertEqual(first, (out / "web_assets.cpp").read_text())


if __name__ == "__main__":
    unittest.main()
