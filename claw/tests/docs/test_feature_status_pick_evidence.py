#!/usr/bin/env python3
"""Keep bounded PICK documentation tied to its actual native assertion."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
IDENT = 'G2-PRD-3-PICK'
EXPECTED = "expected_if_pick_values_stdout=$'11\\n22\\n33\\n44\\n55\\n77\\n101\\n202\\n303\\n404\\n505\\n606\\n0\\n-1\\ninner\\nouter'"


class TestPickEvidence(unittest.TestCase):
    def test_wildcard_outputs_are_not_fixture_inputs(self):
        ledger = (ROOT / 'cat-northstar/FEATURE-STATUS.md').read_text(encoding='utf-8')
        row = next(line for line in ledger.splitlines() if line.startswith('| ' + IDENT + ' |'))
        self.assertIn('[diterima compiler]', row)
        self.assertIn('606, 0, -1', row)
        self.assertNotIn('wildcard 606, 707, 808', row)
        self.assertIn('Tidak membuktikan exhaustiveness', row)
        fixture = (ROOT / 'claw/tests/native/fixtures/revise_if_pick_values.cat').read_text(encoding='utf-8')
        for call in ('pick_with_wildcard(left(606))', 'pick_with_wildcard(right(707))',
                     'pick_statement_with_wildcard(right(808))'):
            self.assertIn(call, fixture)
        self.assertIn('_ {\n            0\n', fixture)
        self.assertIn('_ {\n            selected = -1\n', fixture)

    def test_native_assertion_checks_exact_expected_output(self):
        manifest = json.loads((ROOT / 'cat-northstar/feature-status-atoms.json').read_text(encoding='utf-8'))
        assertion = manifest[IDENT]['assertions'][0]
        runner = (ROOT / assertion['path']).read_text(encoding='utf-8')
        self.assertIn(EXPECTED, runner)
        self.assertIn(assertion['text'], runner)


if __name__ == '__main__':
    unittest.main()
