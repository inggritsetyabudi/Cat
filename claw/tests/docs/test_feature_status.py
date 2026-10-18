#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve()
VALIDATOR = HERE.with_name("validate_feature_status.py")

class TestFeatureStatusValidator(unittest.TestCase):
    def make_repo(self, rows: list[str], prd_numbers=(1, 2), extra="") -> Path:
        temp = Path(tempfile.mkdtemp(prefix="feature-status-test-"))
        self.addCleanup(shutil.rmtree, temp)
        (temp / "cat-northstar").mkdir(); (temp / "claw/tests/frontend/fixtures").mkdir(parents=True)
        (temp / "cat-northstar/PRD.md").write_text("\n".join(f"## {n}. Requirement {n}" for n in prd_numbers) + "\n", encoding="utf-8")
        (temp / "evidence.md").write_text("# Evidence\n\n## Proof\n", encoding="utf-8")
        (temp / "claw/tests/frontend/fixtures/g2.cat").write_text("fixture\n", encoding="utf-8")
        content = "# Feature status\n\n| ID | Sumber | Irisan | Label | Keputusan semantik | Lingkup implementasi | Bukti penerimaan | Sisa | Dimensi non-compiler |\n| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n" + "\n".join(rows) + "\n" + extra
        (temp / "cat-northstar/FEATURE-STATUS.md").write_text(content, encoding="utf-8")
        return temp

    def valid_rows(self, accepted=False):
        sources = ["PRD:1", "PRD:2"] + [f"PLAN:G{i}" for i in range(1, 9)] + [f"PLAN:M{i:02d}" for i in range(1, 11)]
        rows = []
        for i, source in enumerate(sources):
            label = "[diterima compiler]" if accepted and i == 0 else "[diusulkan]"
            scope = "Teruji: fixture boundary" if label == "[diterima compiler]" else "Belum diimplementasikan"
            evidence = "[proof](../evidence.md#proof) [fixture](../claw/tests/frontend/fixtures/g2.cat)" if label == "[diterima compiler]" else "[context](../evidence.md#proof)"
            rows.append(f"| G2-F{i:02d} | {source} | overlap | {label} | keputusan terbatas | {scope} | {evidence} | pending limitation | docs only |")
        return rows

    def validate_cli(self, root: Path):
        return subprocess.run([sys.executable, str(VALIDATOR), "--root", str(root)], text=True, capture_output=True, check=False)

    def test_valid_document_passes_and_resolves_parent_links(self):
        result = self.validate_cli(self.make_repo(self.valid_rows(accepted=True)))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_e2e_reports_failure_and_success(self):
        root = self.make_repo(self.valid_rows())
        bad = self.validate_cli(root); self.assertEqual(bad.returncode, 0, bad.stderr)
        (root / "cat-northstar/FEATURE-STATUS.md").write_text((root / "cat-northstar/FEATURE-STATUS.md").read_text().replace("G2-F00", "BROKEN"), encoding="utf-8")
        failed = self.validate_cli(root); self.assertNotEqual(failed.returncode, 0); self.assertIn("invalid ID", failed.stderr)

    def test_duplicate_and_missing_id_rejected(self):
        rows = self.valid_rows(); rows[1] = rows[1].replace("G2-F01", "G2-F00")
        result = self.validate_cli(self.make_repo(rows)); self.assertIn("duplicate ID", result.stderr)
        rows = self.valid_rows(); rows[0] = rows[0].replace("G2-F00", "")
        self.assertIn("invalid ID", self.validate_cli(self.make_repo(rows)).stderr)

    def test_invalid_and_duplicate_label_rejected(self):
        rows = self.valid_rows(); rows[0] = rows[0].replace("[diusulkan]", "[diputuskan] [diusulkan]")
        result = self.validate_cli(self.make_repo(rows)); self.assertIn("labels may occur only once", result.stderr)
        rows = self.valid_rows(); rows[0] = rows[0].replace("[diusulkan]", "[unknown]")
        self.assertIn("label must be exactly", self.validate_cli(self.make_repo(rows)).stderr)

    def test_malformed_row_without_delimiters_and_bad_cell_count(self):
        rows = self.valid_rows(); rows[0] = rows[0].strip("|")
        self.assertIn("missing pipe delimiters", self.validate_cli(self.make_repo(rows)).stderr)
        rows = self.valid_rows(); rows[0] = "| " + " | ".join(rows[0].strip("|").split("|")[:-1]) + " |"
        self.assertIn("exactly 9 cells", self.validate_cli(self.make_repo(rows)).stderr)

    def test_duplicate_header_and_dynamic_prd_heading(self):
        result = self.validate_cli(self.make_repo(self.valid_rows(), prd_numbers=(1, 2), extra="| ID | Sumber | Irisan | Label | Keputusan semantik | Lingkup implementasi | Bukti penerimaan | Sisa | Dimensi non-compiler |\n"))
        self.assertIn("duplicate FEATURE-STATUS", result.stderr)
        self.assertIn("PRD:3", self.validate_cli(self.make_repo(self.valid_rows(), prd_numbers=(1, 2, 3))).stderr)

    def test_missing_unsafe_and_anchor_evidence_rejected(self):
        rows = self.valid_rows(); rows[0] = rows[0].replace("evidence.md#proof", "missing.md#proof")
        self.assertIn("does not exist", self.validate_cli(self.make_repo(rows)).stderr)
        rows = self.valid_rows(); rows[0] = rows[0].replace("evidence.md#proof", "../../../outside.md#missing")
        result = self.validate_cli(self.make_repo(rows)); self.assertTrue(any(x in result.stderr for x in ("safe relative", "anchor not found", "does not exist")))

    def test_accepted_scope_overclaim_and_bounded_negative(self):
        rows = self.valid_rows(accepted=True); rows[0] = rows[0].replace("Teruji: fixture boundary", "all cases fully supported").replace("keputusan terbatas", "all cases are supported")
        result = self.validate_cli(self.make_repo(rows)); self.assertIn("must start with 'Teruji:'", result.stderr); self.assertIn("broad/unbounded", result.stderr)
        rows = self.valid_rows(accepted=True); rows[0] = rows[0].replace("keputusan terbatas", "tidak diperluas ke seluruh kasus")
        self.assertNotIn("broad/unbounded", self.validate_cli(self.make_repo(rows)).stderr)

    def test_missing_label_and_empty_field(self):
        rows = self.valid_rows(); rows[0] = rows[0].replace("[diusulkan]", "")
        result = self.validate_cli(self.make_repo(rows))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("label must be exactly", result.stderr)
        self.assertIn("every table cell must be nonempty", result.stderr)

    def test_accepted_document_only_evidence_rejected(self):
        rows = self.valid_rows(accepted=True)
        rows[0] = rows[0].replace(" [fixture](../claw/tests/frontend/fixtures/g2.cat)", "")
        result = self.validate_cli(self.make_repo(rows))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("accepted evidence must reference", result.stderr)

    def test_residual_universal_is_not_an_implementation_claim(self):
        rows = self.valid_rows(accepted=True)
        rows[0] = rows[0].replace("pending limitation", "All other expression positions remain unaccepted")
        result = self.validate_cli(self.make_repo(rows, extra="\n## Notes\n\nCoverage includes PRD:1, but that prose is not a row.\n"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bad_anchor_and_missing_evidence(self):
        rows = self.valid_rows(); rows[0] = rows[0].replace("evidence.md#proof", "evidence.md#absent")
        self.assertIn("anchor not found", self.validate_cli(self.make_repo(rows)).stderr)
        rows = self.valid_rows(); rows[0] = rows[0].replace("[context](../evidence.md#proof)", "no linked evidence")
        self.assertIn("needs a local Markdown link", self.validate_cli(self.make_repo(rows)).stderr)

if __name__ == "__main__":
    unittest.main(verbosity=2)
