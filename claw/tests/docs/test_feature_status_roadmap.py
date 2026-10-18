#!/usr/bin/env python3
"""Roadmap coverage must survive coordinated deletion and label drift."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from feature_status_roadmap import SPECS, atom_id, validate_roadmap_inventory
from validate_feature_status import split_row, validate

ROOT = Path(__file__).resolve().parents[3]


class TestRoadmapInventory(unittest.TestCase):
    def setUp(self):
        self.ledger = (ROOT / 'cat-northstar/FEATURE-STATUS.md').read_text(encoding='utf-8')
        self.rows = [(n, split_row(line)) for n, line in enumerate(self.ledger.splitlines(), 1)
                     if line.startswith('| G2-')]
        self.row_map = {cells[0]: cells for _, cells in self.rows}
        self.index = {a['id']: a for a in json.loads((ROOT / 'cat-northstar/feature-status-source-index.json').read_text(encoding='utf-8'))['atoms']}
        self.manifest = json.loads((ROOT / 'cat-northstar/feature-status-atoms.json').read_text(encoding='utf-8'))

    def test_all_independent_source_clauses_have_bounded_proposal_owners(self):
        self.assertEqual(validate_roadmap_inventory(ROOT, self.rows), [])
        source_lines = (ROOT / 'cat-northstar/ROADMAP-v1-plus.md').read_text(encoding='utf-8').splitlines()
        for spec in SPECS:
            ident = atom_id(spec)
            with self.subTest(atom=ident):
                excerpt = '\n'.join(source_lines[50:53]) if ident == 'G2-ROADMAP-3-BYTE-IDENTICAL' else source_lines[spec[2] - 1]
                self.assertEqual(self.index[ident]['source_excerpt'], excerpt)
                self.assertEqual(self.row_map[ident][3], '[diusulkan]')
                self.assertEqual(self.manifest[ident]['fixtures'], [])
                self.assertEqual(self.manifest[ident]['assertions'], [])
                self.assertIn(self.index[ident]['source_anchor'].split('#')[1], self.row_map[ident][6])
                self.assertIn(spec[4], self.row_map[ident][4])

    def test_deletion_from_all_three_surfaces_is_still_rejected(self):
        with tempfile.TemporaryDirectory(prefix='g2-roadmap-inventory-') as directory:
            root = Path(directory)
            shutil.copytree(ROOT / 'cat-northstar', root / 'cat-northstar')
            ident = atom_id(SPECS[0])
            p = root / 'cat-northstar/FEATURE-STATUS.md'
            p.write_text('\n'.join(line for line in self.ledger.splitlines() if not line.startswith('| ' + ident + ' |')) + '\n', encoding='utf-8')
            p = root / 'cat-northstar/feature-status-atoms.json'
            manifest = copy.deepcopy(self.manifest)
            del manifest[ident]
            p.write_text(json.dumps(manifest), encoding='utf-8')
            p = root / 'cat-northstar/feature-status-source-index.json'
            index = json.loads(p.read_text(encoding='utf-8'))
            index['atoms'] = [a for a in index['atoms'] if a['id'] != ident]
            p.write_text(json.dumps(index), encoding='utf-8')
            self.assertTrue(any('missing canonical owner ' + ident in error for error in validate(root)))

    def test_label_scope_and_source_drift_are_rejected(self):
        for field, replacement in ((3, '[diputuskan]'), (4, 'A different contract'), (1, 'ROADMAP:9')):
            with self.subTest(field=field):
                rows = copy.deepcopy(self.rows)
                ident = atom_id(SPECS[0])
                next(c for _, c in rows if c[0] == ident)[field] = replacement
                self.assertTrue(validate_roadmap_inventory(ROOT, rows))

    def test_counts_reconcile_and_ratified_owners_are_not_promoted(self):
        counts = {label: sum(c[3] == label for c in self.row_map.values())
                  for label in ('[diterima compiler]', '[diputuskan]', '[diusulkan]')}
        self.assertEqual(counts['[diterima compiler]'], 54)
        self.assertEqual(counts['[diputuskan]'], 295)
        self.assertEqual(counts['[diusulkan]'], 16 + len(SPECS))
        self.assertIn(f'**{len(self.row_map)} ID**', self.ledger)
        for label, count in counts.items():
            self.assertIn(f'**{count} {label}**', self.ledger)
        for ident in ('G2-PRD-15-REWRITE', 'G2-PLAN-EDITION-STABILITY', 'G2-PRD-20-SEND',
                      'G2-PRD-20-SYNC', 'G2-PRD-20-CLOSURE-MOVE', 'G2-PLAN-ARENA-EXPERIMENTAL'):
            self.assertEqual(self.row_map[ident][3], '[diputuskan]')
        self.assertIn('G2 tetap NOT CLOSED', self.ledger)

    def test_source_header_remains_proposal_not_ratification(self):
        source = (ROOT / 'cat-northstar/ROADMAP-v1-plus.md').read_text(encoding='utf-8')
        self.assertIn('Semua isi dokumen ini berstatus **usulan desain**.', source)
        self.assertIn('self-hosting dimulai setelah v1 terpenuhi', source)
        self.assertNotIn('### Atom proposal', source)
        self.assertEqual(len([c for c in self.row_map.values() if c[0].startswith('G2-ROADMAP-4-REWRITE-')]), 16)


if __name__ == '__main__':
    unittest.main()
