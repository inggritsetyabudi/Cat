#!/usr/bin/env python3
"""Regression and repository sync gate for the generated compact PRD."""
from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
GENERATOR = ROOT / "tools/generate_prd_summary.py"
spec = importlib.util.spec_from_file_location("prd_summary", GENERATOR)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
CANONICAL = (
    "cat-northstar/README.md",
    "cat-northstar/PRD.md",
    "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md",
    "cat-northstar/ROADMAP-v1-plus.md",
    "cat-northstar/legacy/cat-language-design-summary.md",
    "cat-northstar/legacy/cat-language-design-summary_all.md",
)
VALID = "# PRD\n\n## Satu\n\nRingkas: Teks café → aman.\n\nDetail bukan ringkasan.\n\n## Dua\n\nRingkas: Keputusan kedua.\n"


class SummaryTests(unittest.TestCase):
    def render(self, text: str) -> bytes:
        return module.generate(text.encode("utf-8"))

    def reject(self, text: str, diagnostic: str) -> None:
        with self.assertRaisesRegex(ValueError, diagnostic):
            self.render(text)

    def invoke(self, source: Path, output: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(GENERATOR), "--source", str(source), "--output", str(output), *extra],
            capture_output=True, text=True, encoding="utf-8", timeout=30,
            env={**os.environ, "PYTHONUTF8": "1"}, check=False,
        )

    def test_exact_output_only_authored_markers(self) -> None:
        expected = module.HEADER + "## Satu\n\nTeks café → aman.\n\n## Dua\n\nKeputusan kedua.\n"
        self.assertEqual(self.render(VALID), expected.encode("utf-8"))
        self.assertNotIn(b"Detail", self.render(VALID))

    def test_missing_marker_each_section_and_new_section(self) -> None:
        for text in (
            VALID.replace("Ringkas: Teks café → aman.\n", ""),
            VALID.replace("Ringkas: Keputusan kedua.\n", ""),
            VALID + "\n## Tiga\n\nBelum diringkas.\n",
        ):
            with self.subTest(text=text):
                self.reject(text, "missing Ringkas:")

    def test_duplicate_marker(self) -> None:
        self.reject(VALID.replace("Detail bukan ringkasan.", "Ringkas: Duplikat."), "duplicate")

    def test_malformed_marker(self) -> None:
        for marker in ("Ringkas:", "Ringkas: ", "Ringkas:teks", "Ringkas : teks", "ringkas: teks", " Ringkas: teks", "- Ringkas: teks", "**Ringkas:** teks", "Ringkas: teks ", "Ringkas: teks\t"):
            with self.subTest(marker=marker):
                self.reject(VALID.replace("Ringkas: Teks café → aman.", marker), "malformed")

    def test_marker_scope_and_placement(self) -> None:
        self.reject("Ringkas: Preamble.\n" + VALID, "outside")
        self.reject(VALID.replace("Ringkas: Teks café → aman.", "Paragraf.\nRingkas: Terlambat."), "first nonblank")
        self.reject(VALID.replace("Ringkas: Teks café → aman.", "### Anak\nRingkas: Anak."), "first nonblank")
        self.reject(VALID.replace("Ringkas: Teks café → aman.", "```text\nRingkas: Bukan sumber.\n```"), "missing")

    def test_nested_headings_need_no_marker(self) -> None:
        extra = VALID.replace("Detail bukan ringkasan.", "### Anak\n\nIsi anak.")
        self.assertEqual(self.render(extra), self.render(VALID))

    def test_fences_do_not_create_sections_or_markers(self) -> None:
        for fence in ("```", "~~~~"):
            body = f"{fence}text\n## Bukan bagian\nRingkas: Jangan bocor.\n```\n{fence}\n" if fence == "~~~~" else "```text\n## Bukan bagian\nRingkas: Jangan bocor.\n```\n"
            self.assertEqual(self.render(VALID + body), self.render(VALID))
        self.reject(VALID + "```text\n", "unclosed")

    def test_no_sections_and_empty_heading(self) -> None:
        self.reject("# Judul\n", "no level-two")
        self.reject("## \nRingkas: Ada.\n", "empty section")

    def test_encoding_and_newlines(self) -> None:
        expected = self.render(VALID)
        self.assertEqual(self.render(VALID.replace("\n", "\r\n")), expected)
        self.assertEqual(self.render(VALID.rstrip("\n")), expected)
        self.assertNotIn(b"\r", expected)
        self.assertTrue(expected.endswith(b"\n") and not expected.endswith(b"\n\n"))
        with self.assertRaises(UnicodeDecodeError):
            module.generate(b"## Bad\nRingkas: \xff\n")
        self.reject("\ufeff" + VALID, "BOM")
        self.reject(VALID + "\r", "bare CR")

    def test_no_unicode_normalization_or_mojibake_repair(self) -> None:
        text = "e\u0301 café â†’"
        result = self.render(f"## Unicode\n\nRingkas: {text}\n")
        self.assertIn(text.encode("utf-8"), result)

    def test_cli_sync_stale_missing_idempotence_and_source_preservation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cat-prd-summary-") as temporary:
            source, output = Path(temporary) / "source.md", Path(temporary) / "output.md"
            original = VALID.replace("\n", "\r\n").encode("utf-8")
            source.write_bytes(original)
            result = self.invoke(source, output, "--check")
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertFalse(output.exists())
            for _ in range(2):
                result = self.invoke(source, output)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), self.render(VALID))
                self.assertEqual(source.read_bytes(), original)
            before = output.stat().st_mtime_ns
            result = self.invoke(source, output, "--check")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.stat().st_mtime_ns, before)
            for stale in (output.read_bytes() + b"STALE TEXT\n", output.read_bytes().replace(b"\n", b"\r\n"), b"\xff"):
                output.write_bytes(stale)
                self.assertEqual(self.invoke(source, output, "--check").returncode, 1)
                self.assertEqual(output.read_bytes(), stale)
            result = self.invoke(source, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn(b"STALE TEXT", output.read_bytes())
            source.write_bytes(original.replace(b"Keputusan kedua.", b"Keputusan berubah."))
            self.assertEqual(self.invoke(source, output, "--check").returncode, 1)
            self.assertEqual(self.invoke(source, output).returncode, 0)
            self.assertNotIn(b"Keputusan kedua.", output.read_bytes())
            self.assertIn(b"Keputusan berubah.", output.read_bytes())

    def test_cli_invalid_source_never_overwrites_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cat-prd-summary-") as temporary:
            source, output = Path(temporary) / "source.md", Path(temporary) / "output.md"
            for invalid in (b"\xff", b"## Missing\n", b"## Bad\nRingkas: \n"):
                source.write_bytes(invalid)
                output.write_bytes(b"KEEP\n")
                self.assertEqual(self.invoke(source, output).returncode, 1)
                self.assertEqual(output.read_bytes(), b"KEEP\n")
            source.write_bytes(VALID.encode("utf-8"))
            self.assertEqual(self.invoke(source, source).returncode, 1)
            self.assertEqual(source.read_bytes(), VALID.encode("utf-8"))

    def test_repository_canonical_utf8(self) -> None:
        for relative in CANONICAL:
            with self.subTest(path=relative):
                (ROOT / relative).read_bytes().decode("utf-8", errors="strict")

    def test_repository_output_is_exact_generated_content(self) -> None:
        # This is an actual committed-file gate, not only a temporary-fixture test.
        actual = module.OUTPUT.read_bytes()
        actual.decode("utf-8", errors="strict")
        self.assertEqual(actual, module.generate(module.SOURCE.read_bytes()),
                         "PRD-RINGKAS.md is stale; run python3 tools/generate_prd_summary.py")


if __name__ == "__main__":
    unittest.main(verbosity=2)
