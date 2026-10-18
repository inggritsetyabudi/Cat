#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve()
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE.parent))
from feature_status_source_index import validate_source_index  # noqa: E402
from validate_feature_status import validate  # noqa: E402


def canonical_inputs():
    lines = (ROOT / "cat-northstar/FEATURE-STATUS.md").read_text(encoding="utf-8").splitlines()
    rows = []
    for number, line in enumerate(lines, 1):
        if line.startswith("| G2-"):
            rows.append((number, [cell.strip() for cell in line.strip()[1:-1].split("|")]))
    manifest = json.loads((ROOT / "cat-northstar/feature-status-atoms.json").read_text(encoding="utf-8"))
    return rows, manifest


class TestFeatureStatusSourceIndex(unittest.TestCase):
    def setUp(self):
        self.rows, self.manifest = canonical_inputs()
        self.temp = Path(tempfile.mkdtemp(prefix="feature-status-source-index-"))
        self.addCleanup(shutil.rmtree, self.temp)
        (self.temp / "cat-northstar").mkdir()
        for name in ("feature-status-source-index.json", "PRD.md", "PRD-CHANGES-v0.1-to-v1.0.md", "ROADMAP-v1-plus.md"):
            shutil.copy(ROOT / "cat-northstar" / name, self.temp / "cat-northstar" / name)

    def index(self):
        path = self.temp / "cat-northstar/feature-status-source-index.json"
        return json.loads(path.read_text(encoding="utf-8")), path

    def write_index(self, value):
        _, path = self.index()
        path.write_text(json.dumps(value), encoding="utf-8")

    def test_canonical_crosswalk_passes(self):
        self.assertEqual(validate_source_index(self.temp, self.rows, self.manifest), [])

    def test_missing_canonical_crosswalk_fails(self):
        (self.temp / "cat-northstar/feature-status-source-index.json").unlink()
        self.assertTrue(any("missing authoritative source index" in error for error in validate_source_index(self.temp, self.rows, self.manifest)))

    def test_malformed_json_fails(self):
        (self.temp / "cat-northstar/feature-status-source-index.json").write_text("{", encoding="utf-8")
        self.assertTrue(any("invalid authoritative source index" in error for error in validate_source_index(self.temp, self.rows, self.manifest)))

    def test_source_cell_remapping_is_detected_independently(self):
        value, _ = self.index()
        value["atoms"][0]["source"] = "PRD:99"
        self.write_index(value)
        errors = validate_source_index(self.temp, self.rows, self.manifest)
        self.assertTrue(any("source does not match ledger" in error for error in errors))

    def test_manifest_anchor_remapping_is_detected_independently(self):
        value, _ = self.index()
        value["atoms"][0]["source_anchor"] = "cat-northstar/PRD.md#2-literal-koleksi-dan-panjang-array"
        self.write_index(value)
        errors = validate_source_index(self.temp, self.rows, self.manifest)
        self.assertTrue(any("source_anchor does not match manifest" in error for error in errors))

    def test_missing_id_in_crosswalk_is_detected(self):
        value, _ = self.index()
        value["atoms"] = value["atoms"][1:]
        self.write_index(value)
        self.assertTrue(any("index missing ledger ID" in error for error in validate_source_index(self.temp, self.rows, self.manifest)))

    def test_duplicate_crosswalk_id_is_detected(self):
        value, _ = self.index()
        value["atoms"].append(copy.deepcopy(value["atoms"][0]))
        self.write_index(value)
        self.assertTrue(any("duplicate source-index ID" in error for error in validate_source_index(self.temp, self.rows, self.manifest)))

    def test_strict_atom_schema_is_detected(self):
        value, _ = self.index()
        value["atoms"][0]["unexpected"] = True
        self.write_index(value)
        self.assertTrue(any("keys must be exactly" in error for error in validate_source_index(self.temp, self.rows, self.manifest)))

    def test_invalid_schema_does_not_raise_traceback(self):
        value, _ = self.index()
        value["atoms"][0]["source_anchor"] = ["not", "a", "string"]
        self.write_index(value)
        errors = validate_source_index(self.temp, self.rows, self.manifest)
        self.assertTrue(any("source_anchor must be a non-empty string" in error for error in errors))

    def test_excerpt_must_be_verbatim_in_anchored_section(self):
        value, _ = self.index()
        value["atoms"][0]["source_excerpt"] = "not present in PRD"
        self.write_index(value)
        errors = validate_source_index(self.temp, self.rows, self.manifest)
        self.assertTrue(any("source_excerpt is not verbatim" in error for error in errors))

    def test_real_validate_detects_id_removed_from_ledger_and_manifest(self):
        """Exercise validate(root) against a copied repository, not hand-built rows."""
        ignore = shutil.ignore_patterns("artifacts", "__pycache__")
        for directory in ("cat-northstar", "claw/tests", "claw/src", "changelog"):
            shutil.copytree(ROOT / directory, self.temp / directory, ignore=ignore, dirs_exist_ok=True)
        shutil.copy(ROOT / "AGENTS.md", self.temp / "AGENTS.md")
        status = self.temp / "cat-northstar/FEATURE-STATUS.md"
        status.write_text("\n".join(line for line in status.read_text(encoding="utf-8").splitlines()
                                      if "G2-PRD-2-ARRAY-I32" not in line) + "\n", encoding="utf-8")
        manifest_path = self.temp / "cat-northstar/feature-status-atoms.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        del manifest["G2-PRD-2-ARRAY-I32"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        errors = validate(self.temp)
        self.assertTrue(any("index has ID(s) absent from ledger" in error or "index has ID(s) absent from manifest" in error
                            for error in errors), errors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
