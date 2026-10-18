#!/usr/bin/env python3
from __future__ import annotations
import importlib.util
import sys
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve()
spec = importlib.util.spec_from_file_location("feature_status_examples", HERE.with_name("feature_status_examples.py"))
scanner = importlib.util.module_from_spec(spec)
assert spec.loader
sys.modules[spec.name] = scanner
spec.loader.exec_module(scanner)


def fence(body: str, info: str = "cat") -> scanner.Fence:
    return scanner.Fence(Path("PRD.md"), 10, info, body)


class TestFeatureStatusExamples(unittest.TestCase):
    def test_g3_rejects_multiline_named_constructor_but_allows_new_and_shape_literal(self):
        bad = scanner.scan_fence(fence("val x = Config(\n  host: \"a\",\n  port: 80\n)\n"))
        self.assertTrue(any(item.rule == "G3" for item in bad))
        good = scanner.scan_fence(fence("val x = Config.new(\n  \"a\", 80\n)\nval y = Config { host: \"a\", port: 80 }\n"))
        self.assertFalse(any(item.rule == "G3" for item in good), good)

    def test_g4_checks_declarations_and_choice_variants_not_types_or_generics(self):
        bad = scanner.scan_fence(fence("""shape Config { share host: Str }
choice Shape { Circle(Int32), good }
fn Bad[T](Arg: Int32) -> Int32 { val Value = Arg }
module Upper { }
"""))
        rules = [item for item in bad if item.rule == "G4"]
        self.assertGreaterEqual(len(rules), 5)
        good = scanner.scan_fence(fence("""shape Config { share host: Int32 }
choice Result { Ok(Int32), Fail, Some(Int32), None }
fn good[T with Comparable](arg: Int32) -> Int32 { val value = arg }
"""))
        self.assertFalse([item for item in good if item.rule == "G4"], good)

    def test_comments_and_strings_are_not_code(self):
        body = '''// Config(host: Int32) Option pub realm\nval text = "Option(pub realm Config(host: Int32))"\n/* val Bad = 1; */\nval good = 1\n'''
        self.assertEqual(scanner.scan_fence(fence(body)), [])

    def test_option_and_legacy_keywords_are_universal_in_expected_error_fences(self):
        findings = scanner.scan_universal_tokens(fence('''pub realm\nval x = Option[Int32]\n''', "cat error: expected"))
        self.assertEqual({item.rule for item in findings}, {"G5", "G8"})

    def test_expected_error_excludes_declaration_checks_but_reports_category(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "cat-northstar").mkdir(parents=True)
            (root / "cat-northstar/PRD.md").write_text("```cat error: stale\nval Bad = 1\n```\n", encoding="utf-8")
            report = scanner.scan_repository(root)
            self.assertEqual(report.excluded["expected-error"], ["cat-northstar/PRD.md:1"])
            self.assertFalse(report.findings)

    def test_tilde_fences_and_explicit_inventory_skip_history(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "cat-northstar").mkdir(parents=True)
            (root / "cat-northstar/PRD.md").write_text("~~~cat\nval Bad = 1\n~~~\n", encoding="utf-8")
            (root / "changelog").mkdir()
            (root / "changelog/old.md").write_text("```cat\nval Bad = 1\n```\n", encoding="utf-8")
            report = scanner.scan_repository(root)
            self.assertEqual(len(report.findings), 1)
            self.assertEqual(report.findings[0].rule, "G4")
            self.assertEqual(report.inventory, ["cat-northstar/PRD.md"])

    def test_g8_checks_exact_modules_cat_but_ignores_strings_comments_and_near_matches(self):
        self.assertEqual([item.rule for item in scanner.scan_fence(fence("import modules.cat\n"))], ["G8"])
        self.assertEqual([item.rule for item in scanner.scan_fence(fence("import src.modules.cat\n"))], ["G8"])
        good = scanner.scan_fence(fence('// modules.cat\nval text = "modules.cat"\nval near = modules.cater\n'))
        self.assertFalse(good, good)

    def test_option_is_g5_and_type_ref_is_not_a_g4_declaration(self):
        findings = scanner.scan_fence(fence("val x = Maybe[ref Node]\nval y = Option[Int32]\n"))
        self.assertEqual([item.rule for item in findings], ["G5"])

    def test_ref_parameter_and_uppercase_function_signature_are_bounded(self):
        findings = scanner.scan_fence(fence("fn good(ref User) -> Int32 { 1 }\n"))
        self.assertTrue(any(item.rule == "G4" for item in findings), findings)
        signature = scanner.scan_fence(fence("fn Bad(Arg: Int32) -> Int32 { 1 }\n"))
        self.assertFalse(any(item.rule == "G3" for item in signature), signature)

    def test_repository_scans_active_fences_not_stale_prose_and_counts_proposals(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / "README.md"
            path.write_text("stale prose modules.cat and Option\n```cat\nimport modules.cat\n```\n", encoding="utf-8")
            report = scanner.scan_repository(root)
            self.assertEqual({item.rule for item in report.findings}, {"G8"})
            path.write_text("```cat proposal\nimport modules.cat\n```\n", encoding="utf-8")
            report = scanner.scan_repository(root)
            self.assertEqual({item.rule for item in report.findings}, {"G8"})
            self.assertEqual(report.scanned["cat proposal"], ["README.md:1"])

    def test_cli_prose_is_ignored_but_stale_proposal_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "README.md"
            path.write_text("Migration forbids `Option`, `pub`, `realm`, `modules.cat`, and `Config(host: ...)`.\n", encoding="utf-8")
            command = [sys.executable, str(HERE.with_name("feature_status_examples.py")), "--root", temp]
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            path.write_text("```cat proposal\nval stale = Config(host: 1)\nval opt: Option[Int32] = None\nimport modules.cat\n```\n", encoding="utf-8")
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stdout)
            for rule in ("G3", "G5", "G8"):
                self.assertIn(f"[{rule}]", result.stderr)


if __name__ == "__main__":
    unittest.main()
