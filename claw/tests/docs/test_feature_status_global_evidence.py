#!/usr/bin/env python3
"""Bounded documentation evidence; no compiler feature acceptance implied."""
from pathlib import Path
import unittest

from run_doc_tests import read_fences

ROOT = Path(__file__).resolve().parents[3]


class TestGlobalEvidence(unittest.TestCase):
    def test_entrypoints_are_distinct_executable_fences(self):
        path = ROOT / 'cat-northstar/PRD.md'
        fences = list(read_fences(path))
        canonical = [(line, body) for line, info, body in fences
                     if info == 'cat' and body.strip() == 'fn main() -> Int32 { return 0 }']
        unit = [(line, body) for line, info, body in fences
                if info == 'cat' and body.strip() == 'fn main() {}']
        self.assertEqual(len(canonical), 1, 'one canonical executable example required')
        self.assertEqual(len(unit), 1, 'Unit needs its own executable example')
        self.assertNotEqual(canonical[0][0], unit[0][0])
        self.assertIn('**Varian sah — Unit:**', path.read_text(encoding='utf-8'))

    def test_g7_is_three_documentation_obligations(self):
        import json
        path = ROOT / 'cat-northstar/feature-status-atoms.json'
        manifest = json.loads(path.read_text(encoding='utf-8'))
        ledger = (ROOT / 'cat-northstar/FEATURE-STATUS.md').read_text(encoding='utf-8')
        for ident in ('G2-PLAN-G7', 'G2-PLAN-G7-RINGKAS-SOURCE',
                      'G2-PLAN-G7-GENERATOR-PROVENANCE'):
            self.assertIn(ident, manifest)
            row = next(line for line in ledger.splitlines() if line.startswith('| ' + ident + ' |'))
            self.assertIn('[diputuskan]', row)
            self.assertEqual(manifest[ident]['fixtures'], [])
        self.assertIn('PR #26 terbuka/unmerged', ledger)

    def test_archival_encoding_is_not_generated_output_evidence(self):
        for name in ('cat-language-design-summary.md', 'cat-language-design-summary_all.md'):
            raw = (ROOT / 'cat-northstar/legacy' / name).read_bytes()
            self.assertEqual(raw.decode('utf-8').encode('utf-8'), raw)
        # This checks archival bytes only. Generator availability is a residual,
        # not a requirement that prevents a future generator from being added.


if __name__ == '__main__':
    unittest.main()
