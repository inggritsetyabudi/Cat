#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import shutil
import json
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
        (temp / "cat-northstar/ROADMAP-v1-plus.md").write_text("# Post-v1\n\n## 4. v1.3 rewrite macro\n\nRoadmap proposal text.\n\n## 5. v1.4 threading\n\nLater proposal.\n", encoding="utf-8")
        plan = ["## 2. Perubahan global\n", "| # | Perubahan | Versi |\n", "| --- | --- | --- |\n"] + [f"| G{i} | change | v0.1 |\n" for i in range(1, 9)] + ["## 4. Milestone final v0.1 sampai v1.0\n", "| Versi | Isi | Bukti |\n", "| --- | --- | --- |\n"] + [f"| {'v1.0' if i == 10 else f'v0.{i}'} | milestone | proof |\n" for i in range(1, 11)]
        (temp / "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md").write_text("".join(plan), encoding="utf-8")
        (temp / "evidence.md").write_text("# Evidence\n\n## Proof\n", encoding="utf-8")
        (temp / "claw/tests/frontend/fixtures/g2.cat").write_text("fixture\n", encoding="utf-8")
        (temp / "claw/tests/frontend/run_fixture.sh").write_text("run g2.cat\nassertion text\n", encoding="utf-8")
        content = "# Feature status\n\n| ID | Sumber | Irisan | Label | Keputusan semantik | Lingkup implementasi | Bukti penerimaan | Sisa | Dimensi non-compiler |\n| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n" + "\n".join(rows) + "\n" + extra
        (temp / "cat-northstar/FEATURE-STATUS.md").write_text(content, encoding="utf-8")
        atoms = {}
        for i, source in enumerate(["PRD:1", "PRD:2"] + [f"PLAN:G{i}" for i in range(1, 9)] + [f"PLAN:M{i:02d}" for i in range(1, 11)]):
            atom = {"source_anchor": f"cat-northstar/PRD.md#{source.split(':')[1]}-requirement-{source.split(':')[1]}" if source.startswith("PRD:") else ("cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#2-perubahan-global" if source.startswith("PLAN:G") else "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#4-milestone-final-v01-sampai-v10"), "fixtures": [], "assertions": []}
            if i == 0 and any("[diterima compiler]" in row for row in rows): atom.update(fixtures=["claw/tests/frontend/fixtures/g2.cat"], assertions=[{"path": "claw/tests/frontend/run_fixture.sh", "text": "assertion text"}])
            atoms[f"G2-F{i:02d}"] = atom
        (temp / "cat-northstar/feature-status-atoms.json").write_text(json.dumps(atoms), encoding="utf-8")
        index = {"schema_version": 1, "authority": "Test canonical crosswalk", "maintenance": "Independent fixture inventory", "boundaries": "No closure claim", "atoms": []}
        for i, (ident, atom) in enumerate(atoms.items()):
            source = (["PRD:1", "PRD:2"] + [f"PLAN:G{j}" for j in range(1, 9)] + [f"PLAN:M{j:02d}" for j in range(1, 11)])[i]
            index["atoms"].append({"id": ident, "source": source, "source_anchor": atom["source_anchor"], "requirement_description": "Test requirement", "source_excerpt": "Requirement" if source.startswith("PRD:") else ("change" if source.startswith("PLAN:G") else "milestone")})
        (temp / "cat-northstar/feature-status-source-index.json").write_text(json.dumps(index), encoding="utf-8")
        return temp

    def valid_rows(self, accepted=False):
        sources = ["PRD:1", "PRD:2"] + [f"PLAN:G{i}" for i in range(1, 9)] + [f"PLAN:M{i:02d}" for i in range(1, 11)]
        rows = []
        for i, source in enumerate(sources):
            label = "[diterima compiler]" if accepted and i == 0 else "[diusulkan]"
            scope = "Teruji: fixture boundary" if label == "[diterima compiler]" else "Belum diimplementasikan"
            evidence = "[proof](../evidence.md#proof) [fixture](../claw/tests/frontend/fixtures/g2.cat) [runner](../claw/tests/frontend/run_fixture.sh)" if label == "[diterima compiler]" else "[context](../evidence.md#proof)"
            if source.startswith("PRD:"):
                number = source.split(":")[1]
                evidence += f" [source](PRD.md#{number}-requirement-{number})"
            else:
                anchor = "2-perubahan-global" if source.startswith("PLAN:G") else "4-milestone-final-v01-sampai-v10"
                evidence += f" [source](PRD-CHANGES-v0.1-to-v1.0.md#{anchor})"
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
        self.assertTrue(any(x in result.stderr for x in ("accepted evidence must reference", "fixture is not linked")))

    def test_residual_universal_is_not_an_implementation_claim(self):
        rows = self.valid_rows(accepted=True)
        rows[0] = rows[0].replace("pending limitation", "All other expression positions remain unaccepted")
        result = self.validate_cli(self.make_repo(rows, extra="\n## Notes\n\nCoverage includes PRD:1, but that prose is not a row.\n"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bad_anchor_and_missing_evidence(self):
        rows = self.valid_rows(); rows[0] = rows[0].replace("evidence.md#proof", "evidence.md#absent")
        self.assertIn("anchor not found", self.validate_cli(self.make_repo(rows)).stderr)
        rows = self.valid_rows(); rows[0] = rows[0].replace("[context](../evidence.md#proof)", "no linked evidence")
        rows[0] = rows[0].replace(" [source](PRD.md#1-requirement-1)", "")
        self.assertIn("needs a local Markdown link", self.validate_cli(self.make_repo(rows)).stderr)

    def test_manifest_rejects_wrong_heading_missing_atom_and_evidence_mismatches(self):
        root = self.make_repo(self.valid_rows(accepted=True))
        manifest = json.loads((root / "cat-northstar/feature-status-atoms.json").read_text())
        manifest["G2-F00"]["source_anchor"] = "cat-northstar/PRD.md#2-requirement-2"
        (root / "cat-northstar/feature-status-atoms.json").write_text(json.dumps(manifest))
        self.assertIn("does not match PRD:1", self.validate_cli(root).stderr)
        del manifest["G2-F01"]
        (root / "cat-northstar/feature-status-atoms.json").write_text(json.dumps(manifest))
        self.assertIn("manifest missing atom", self.validate_cli(root).stderr)
        manifest["G2-F01"] = {"source_anchor": "cat-northstar/PRD.md#2-requirement-2", "fixtures": [], "assertions": []}
        manifest["G2-F00"]["fixtures"] = ["claw/tests/frontend/fixtures/missing.cat"]
        (root / "cat-northstar/feature-status-atoms.json").write_text(json.dumps(manifest))
        result = self.validate_cli(root)
        self.assertTrue(any(x in result.stderr for x in ("fixture does not exist", "fixture is not linked")))

    def test_manifest_rejects_runner_and_assertion_mismatch(self):
        root = self.make_repo(self.valid_rows(accepted=True))
        manifest = json.loads((root / "cat-northstar/feature-status-atoms.json").read_text())
        manifest["G2-F00"]["assertions"][0]["text"] = "not present"
        (root / "cat-northstar/feature-status-atoms.json").write_text(json.dumps(manifest))
        self.assertIn("assertion text not found", self.validate_cli(root).stderr)

    def test_manifest_schema_types_and_empty_accepted_arrays_do_not_crash(self):
        root = self.make_repo(self.valid_rows(accepted=True))
        path = root / "cat-northstar/feature-status-atoms.json"; manifest = json.loads(path.read_text())
        manifest["G2-F00"]["fixtures"] = "not-an-array"; manifest["G2-F00"]["assertions"] = ["bad-item"]
        path.write_text(json.dumps(manifest)); result = self.validate_cli(root)
        self.assertNotEqual(result.returncode, 0); self.assertIn("fixtures must be", result.stderr)
        manifest["G2-F00"]["fixtures"] = []; manifest["G2-F00"]["assertions"] = []
        path.write_text(json.dumps(manifest)); self.assertIn("must be nonempty", self.validate_cli(root).stderr)

    def test_manifest_requires_exact_fixture_and_runner_links(self):
        root = self.make_repo(self.valid_rows(accepted=True)); path = root / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(path.read_text()); manifest["G2-F00"]["fixtures"] = ["claw/tests/frontend/other/g2.cat"]
        (root / "claw/tests/frontend/other").mkdir(); (root / "claw/tests/frontend/other/g2.cat").write_text("x")
        path.write_text(json.dumps(manifest)); self.assertIn("fixture is not linked", self.validate_cli(root).stderr)
        rows = self.valid_rows(accepted=True); rows[0] = rows[0].replace(" [runner](../claw/tests/frontend/run_fixture.sh)", "")
        (root / "cat-northstar/FEATURE-STATUS.md").write_text((root / "cat-northstar/FEATURE-STATUS.md").read_text().replace(self.valid_rows(accepted=True)[0], rows[0]))
        self.assertIn("runner is not linked", self.validate_cli(root).stderr)

    def test_plan_actual_mapping_and_wrong_section_are_checked(self):
        root = self.make_repo(self.valid_rows()); path = root / "cat-northstar/feature-status-atoms.json"; manifest = json.loads(path.read_text())
        manifest["G2-F02"]["source_anchor"] = "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#4-milestone-final-v01-sampai-v10"; path.write_text(json.dumps(manifest))
        self.assertIn("§2 heading", self.validate_cli(root).stderr)

    def test_malformed_sources_report_errors_without_traceback(self):
        for bad in ("PRD:x", "PLAN:Gx", "PLAN:Mxx", "ROADMAP:x", "missing-colon"):
            with self.subTest(source=bad):
                rows = self.valid_rows(); rows[0] = rows[0].replace("PRD:1", bad)
                result = self.validate_cli(self.make_repo(rows))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid source", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_existing_but_unrelated_runner_assertion_is_rejected(self):
        root = self.make_repo(self.valid_rows(accepted=True))
        (root / "claw/tests/frontend/run_fixture.sh").write_text(
            'run_check_pass "first" "g2.cat"\nfirst assertion\n'
            'run_check_pass "second" "other.cat"\nother assertion\n')
        path = root / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(path.read_text())
        manifest["G2-F00"]["assertions"][0]["text"] = "other assertion"
        path.write_text(json.dumps(manifest))
        result = self.validate_cli(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("same bounded runner block", result.stderr)
        manifest["G2-F00"]["assertions"][0]["text"] = "first assertion"
        path.write_text(json.dumps(manifest))
        self.assertEqual(self.validate_cli(root).returncode, 0)

    def test_continued_command_assertion_is_accepted(self):
        root = self.make_repo(self.valid_rows(accepted=True))
        command = 'run_check_fail "first" ' + '\\' + '\n  "g2.cat" ' + '\\' + '\n  "expected diagnostic"'
        (root / "claw/tests/frontend/run_fixture.sh").write_text(command + "\n")
        path = root / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(path.read_text())
        manifest["G2-F00"]["assertions"][0]["text"] = command
        path.write_text(json.dumps(manifest))
        result = self.validate_cli(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def implicit_source_repo(self, suffix="TAIL", ident=None, source="PLAN:M02", anchor=None):
        root = self.make_repo(self.valid_rows())
        ident = ident or f"G2-PLAN-M02-IMPLICIT-{suffix}"
        anchor = anchor or "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#3-perubahan-per-bagian-prd"
        plan_path = root / "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md"
        plan = plan_path.read_text()
        section = "## 3. Perubahan per bagian PRD\n\n| 33 | Implicit return | Hanya ekspresi terakhir blok | D v0.1, K v0.2 | ekspresi di tengah blok → peringatan |\n\n"
        plan_path.write_text(plan.replace("## 4.", section + "## 4.", 1))
        ledger = root / "cat-northstar/FEATURE-STATUS.md"
        link = anchor.removeprefix("cat-northstar/")
        ledger.write_text(ledger.read_text() + f"| {ident} | {source} | implicit return | [diputuskan] | keputusan terbatas | Belum dikreditkan | [source]({link}) | pending limitation | docs only |\n")
        manifest_path = root / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(manifest_path.read_text())
        manifest[ident] = {"source_anchor": anchor, "fixtures": [], "assertions": []}
        manifest_path.write_text(json.dumps(manifest))
        index_path = root / "cat-northstar/feature-status-source-index.json"
        index = json.loads(index_path.read_text())
        excerpt = "Hanya ekspresi terakhir blok" if suffix == "TAIL" else "ekspresi di tengah blok → peringatan"
        if "#4-" in anchor:
            excerpt = "milestone"
        index["atoms"].append({"id": ident, "source": source, "source_anchor": anchor, "requirement_description": "Test implicit-return requirement", "source_excerpt": excerpt})
        index_path.write_text(json.dumps(index))
        return root

    def test_implicit_normative_section3_passes_for_both_exact_ids(self):
        for suffix in ("TAIL", "WARNING"):
            with self.subTest(suffix=suffix):
                result = self.validate_cli(self.implicit_source_repo(suffix))
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_implicit_exception_does_not_apply_to_other_ids(self):
        for ident in ("G2-PLAN-M02", "G2-PLAN-M02-IMPLICIT-OTHER", "G2-PLAN-M02-IMPLICIT-TAIL-EXTRA"):
            with self.subTest(ident=ident):
                result = self.validate_cli(self.implicit_source_repo(ident=ident))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("§4 heading", result.stderr)

    def test_implicit_exception_requires_m02_staging(self):
        for suffix in ("TAIL", "WARNING"):
            with self.subTest(suffix=suffix):
                result = self.validate_cli(self.implicit_source_repo(suffix, source="PLAN:M03"))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires PLAN:M02 staging", result.stderr)

    def test_implicit_old_milestone_anchor_is_not_normative(self):
        for suffix in ("TAIL", "WARNING"):
            with self.subTest(suffix=suffix):
                result = self.validate_cli(self.implicit_source_repo(suffix, anchor="cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#4-milestone-final-v01-sampai-v10"))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("normative §3 implicit-return source", result.stderr)

    def test_implicit_requires_exact_rule_and_warning_in_section3(self):
        for old, new in (("Hanya ekspresi terakhir blok", "Setiap ekspresi blok"), ("ekspresi di tengah blok → peringatan", "tanpa peringatan"), ("| 33 | Implicit return", "| 34 | Implicit return")):
            with self.subTest(mutation=old):
                root = self.implicit_source_repo()
                path = root / "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md"
                path.write_text(path.read_text().replace(old, new))
                result = self.validate_cli(root)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("exact normative implicit-return row not found", result.stderr)

    def test_implicit_rule_outside_section3_is_rejected(self):
        root = self.implicit_source_repo()
        path = root / "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md"
        text = path.read_text()
        row = next(line for line in text.splitlines() if line.startswith("| 33 |"))
        path.write_text(text.replace(row, "") + "\n" + row + "\n")
        result = self.validate_cli(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exact normative implicit-return row not found", result.stderr)

    def plan_s_repo(self, source="PLAN:S55", anchor="4-milestone-final-v01-sampai-v10", excerpt="S55 docs-only"):
        root = self.make_repo(self.valid_rows())
        plan_path = root / "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md"
        plan_lines = plan_path.read_text().splitlines()
        while len(plan_lines) < 54:
            plan_lines.append("filler")
        plan_lines.append("| S55 docs-only | governance |")
        plan_path.write_text("\n".join(plan_lines) + "\n")
        ident = "G2-PLAN-S55"
        source_anchor = f"cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#{anchor}"
        ledger = root / "cat-northstar/FEATURE-STATUS.md"
        ledger.write_text(ledger.read_text() + f"| {ident} | {source} | governance | [diputuskan] | keputusan terbatas | Docs-only; tidak dikreditkan ke compiler. | [source](PRD-CHANGES-v0.1-to-v1.0.md#{anchor}) | pending limitation | governance docs |\n")
        manifest_path = root / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(manifest_path.read_text())
        manifest[ident] = {"source_anchor": source_anchor, "fixtures": [], "assertions": []}
        manifest_path.write_text(json.dumps(manifest))
        index_path = root / "cat-northstar/feature-status-source-index.json"
        index = json.loads(index_path.read_text())
        index["atoms"].append({"id": ident, "source": source, "source_anchor": source_anchor, "requirement_description": "Docs-only governance requirement", "source_excerpt": excerpt})
        index_path.write_text(json.dumps(index))
        return root

    def test_plan_s_valid_line_anchor_and_excerpt_pass(self):
        result = self.validate_cli(self.plan_s_repo())
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_plan_s_nonexistent_line_is_rejected(self):
        result = self.validate_cli(self.plan_s_repo(source="PLAN:S999"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source line is not enclosed", result.stderr)

    def test_plan_s_mismatched_anchor_is_rejected(self):
        result = self.validate_cli(self.plan_s_repo(anchor="2-perubahan-global"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source line is not enclosed", result.stderr)

    def test_plan_s_excerpt_must_match_source_line(self):
        for excerpt in ("not in source line", "filler"):
            with self.subTest(excerpt=excerpt):
                result = self.validate_cli(self.plan_s_repo(excerpt=excerpt))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("source_excerpt must be a substring", result.stderr)

    def test_plan_s_malformed_line_is_rejected_without_traceback(self):
        for source in ("PLAN:Sbad", "PLAN:S", "PLAN:S-1", "PLAN:S0"):
            with self.subTest(source=source):
                result = self.validate_cli(self.plan_s_repo(source=source))
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("Traceback", result.stderr)
                self.assertNotIn("SyntaxError", result.stderr)
                self.assertIn("FEATURE-STATUS: FAIL", result.stdout)

    def test_plan_s_cannot_be_accepted_by_compiler(self):
        root = self.plan_s_repo()
        path = root / "cat-northstar/FEATURE-STATUS.md"
        text = path.read_text().replace("| G2-PLAN-S55 | PLAN:S55 | governance | [diputuskan]", "| G2-PLAN-S55 | PLAN:S55 | governance | [diterima compiler]")
        path.write_text(text)
        result = self.validate_cli(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("documentation-only", result.stderr)

    def roadmap_repo(self, source="ROADMAP:4", anchor="4-v13-rewrite-macro", label="[diusulkan]", path=None):
        root = self.make_repo(self.valid_rows())
        ident = "G2-ROADMAP-4-REWRITE-PROPOSAL"
        source_path = path or "cat-northstar/ROADMAP-v1-plus.md"
        source_anchor = f"{source_path}#{anchor}"
        ledger = root / "cat-northstar/FEATURE-STATUS.md"
        ledger.write_text(ledger.read_text() + f"| {ident} | {source} | rewrite proposal | {label} | proposal only | Belum diimplementasikan | [source]({source_anchor.removeprefix('cat-northstar/')}) | pending ratification | governance docs |\n")
        manifest_path = root / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(manifest_path.read_text())
        manifest[ident] = {"source_anchor": source_anchor, "fixtures": [], "assertions": []}
        manifest_path.write_text(json.dumps(manifest))
        index_path = root / "cat-northstar/feature-status-source-index.json"
        index = json.loads(index_path.read_text())
        index["atoms"].append({"id": ident, "source": source, "source_anchor": source_anchor, "requirement_description": "Roadmap design proposal", "source_excerpt": "Roadmap proposal text."})
        index_path.write_text(json.dumps(index))
        return root

    def test_roadmap_numeric_section_and_exact_file_pass(self):
        result = self.validate_cli(self.roadmap_repo())
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_roadmap_wrong_file_or_heading_section_is_rejected(self):
        cases = (("cat-northstar/PRD.md", "1-requirement-1", "must target ROADMAP-v1-plus.md"),
                 ("cat-northstar/ROADMAP-v1-plus.md", "5-v14-threading", "does not match ROADMAP:4"),
                 ("cat-northstar/ROADMAP-v1-plus.md", "6-missing", "heading not found"))
        for path, anchor, expected in cases:
            with self.subTest(path=path, anchor=anchor):
                result = self.validate_cli(self.roadmap_repo(path=path, anchor=anchor))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)

    def test_roadmap_proposal_cannot_escalate_label(self):
        for label in ("[diputuskan]", "[diterima compiler]"):
            with self.subTest(label=label):
                result = self.validate_cli(self.roadmap_repo(label=label))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("ROADMAP sources are design proposals", result.stderr)

    def test_roadmap_source_index_source_mismatch_is_rejected(self):
        root = self.roadmap_repo()
        path = root / "cat-northstar/feature-status-source-index.json"
        index = json.loads(path.read_text())
        index["atoms"][-1]["source"] = "PRD:15"
        path.write_text(json.dumps(index))
        result = self.validate_cli(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source does not match ledger", result.stderr)

    def test_roadmap_source_index_anchor_mismatch_is_rejected(self):
        root = self.roadmap_repo()
        path = root / "cat-northstar/feature-status-source-index.json"
        index = json.loads(path.read_text())
        index["atoms"][-1]["source_anchor"] = "cat-northstar/ROADMAP-v1-plus.md#5-v14-threading"
        path.write_text(json.dumps(index))
        result = self.validate_cli(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source_anchor does not match manifest", result.stderr)

    def test_roadmap_unknown_numeric_section_is_rejected(self):
        result = self.validate_cli(self.roadmap_repo(source="ROADMAP:99", anchor="99-missing"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("heading not found", result.stderr)

if __name__ == "__main__":
    unittest.main(verbosity=2)
