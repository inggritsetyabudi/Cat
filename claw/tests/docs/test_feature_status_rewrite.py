#!/usr/bin/env python3
"""Focused real-source checks: ratified stage is not proposed macro semantics."""
from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'cat-northstar'
PREFIX = 'G2-ROADMAP-4-REWRITE-'
ANCHOR = 'cat-northstar/ROADMAP-v1-plus.md#4-v13-rewrite-macro'
# Static reviewed ownership, deliberately not generated from the ledger/manifest.
OWNERS = {
    'EXPANSION-ORDER': 'Ekspansi terjadi **sebelum** pengecekan tipe',
    'EXPANSION-AST': 'hasilnya AST biasa',
    'TYPE-CHECK': 'lalu dicek seperti kode tulisan tangan',
    'SAFE-CHECK': 'hasil macro tidak punya jalan pintas Safe',
    'HYGIENE': 'nama yang dibuat di dalam macro tidak bisa bentrok',
    'NO-IO': 'melakukan I/O',
    'NO-EXTERNAL': 'memanggil kode luar saat ekspansi',
    'DEPTH-LIMIT': 'kedalaman dan jumlah node',
    'NODE-LIMIT': 'kedalaman dan jumlah node',
    'CALLSITE-DIAGNOSTIC': 'tempat pemanggilan',
    'EXPANSION-DIAGNOSTIC': 'menyebut ekspansinya',
    'USER-ANNOTATION-ENGINE': 'dibangun di atas mesin yang sama',
    'EXPAND-TOOL': '`claw expand` menampilkan hasil ekspansi',
    'SIZE-LINT': 'lint untuk macro yang menghasilkan lebih dari N node',
    'SOURCE-SPAN': 'Setiap node hasil macro membawa lokasi asal',
    'AST-ONLY-ENGINE': 'Ekspander hanya memanipulasi AST; tidak ada logika sembarang',
}


class TestRewriteSourceOwnership(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ledger = (BASE / 'FEATURE-STATUS.md').read_text(encoding='utf-8')
        cls.rows = {}
        for line in cls.ledger.splitlines():
            if line.startswith('| G2-'):
                cells = [cell.strip() for cell in line.strip('|').split('|')]
                if cells[0] in cls.rows:
                    raise AssertionError('duplicate canonical atom: ' + cells[0])
                cls.rows[cells[0]] = cells
        cls.manifest = json.loads((BASE / 'feature-status-atoms.json').read_text(encoding='utf-8'))
        cls.index = {item['id']: item for item in json.loads((BASE / 'feature-status-source-index.json').read_text(encoding='utf-8'))['atoms']}

    def test_each_proposed_rule_has_one_canonical_owner_and_verbatim_source(self):
        expected = {PREFIX + suffix for suffix in OWNERS}
        self.assertEqual({key for key in self.rows if key.startswith(PREFIX)}, expected)
        roadmap = (BASE / 'ROADMAP-v1-plus.md').read_text(encoding='utf-8')
        section = roadmap.split('## 4. v1.3 `rewrite` (macro)', 1)[1].split('\n## 5.', 1)[0]
        for suffix, phrase in OWNERS.items():
            ident = PREFIX + suffix
            with self.subTest(atom=ident):
                self.assertEqual(self.rows[ident][1], 'ROADMAP:4')
                self.assertEqual(self.rows[ident][3], '[diusulkan]')
                self.assertEqual(self.manifest[ident], {'source_anchor': ANCHOR, 'fixtures': [], 'assertions': []})
                entry = self.index[ident]
                self.assertEqual(entry['source'], 'ROADMAP:4')
                self.assertEqual(entry['source_anchor'], ANCHOR)
                self.assertIn(entry['source_excerpt'], section)
                self.assertIn(phrase, entry['source_excerpt'])
                self.assertIn(entry['requirement_description'], self.rows[ident][4])

    def test_ratified_stage_is_separate_from_semantics_and_generic_gate(self):
        ident = 'G2-PRD-15-REWRITE'
        row = self.rows[ident]
        self.assertEqual(row[1], 'PRD:15')
        self.assertEqual(row[3], '[diputuskan]')
        self.assertIn('Hanya staging/keberadaan rewrite', row[4])
        self.assertIn('✅ Diputuskan: masuk **v1.x**, bukan v1.', self.index[ident]['source_excerpt'])
        self.assertEqual(self.manifest[ident]['source_anchor'], 'cat-northstar/PRD.md#15-foreign-c-annotation-dan-rewrite')
        self.assertIn('header ROADMAP', row[6])
        self.assertIn('S80 hanya provenance migrasi historis', row[7])
        self.assertEqual(self.rows['G2-PLAN-M10'][1], 'PLAN:M10')
        self.assertIn('G2 tetap NOT CLOSED', self.ledger)
        self.assertNotIn('Discrepancy untuk rekonsiliasi terpisah', self.ledger)

    def test_roadmap_header_and_historical_migration_are_not_rewritten(self):
        roadmap = (BASE / 'ROADMAP-v1-plus.md').read_text(encoding='utf-8')
        self.assertIn('Belum ada yang diputuskan kecuali: `rewrite` (macro) ada di v1.x', roadmap)
        plan = (BASE / 'PRD-CHANGES-v0.1-to-v1.0.md').read_text(encoding='utf-8')
        self.assertIn('Label `[diusulkan]`, dipindah ke dokumen roadmap v1+', plan)
        self.assertIn('tidak ada contoh `cat` di bagian ini', plan)


if __name__ == '__main__':
    unittest.main()
