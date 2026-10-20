#!/usr/bin/env python3
"""Focused source-preflight tests; no builds, native suite or artifact mutation."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

import windows_stack_source_guard as guard

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'original-source' if (ROOT / 'original-source' / '.git').exists() else ROOT


class SourceGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = guard.load_manifest(Path(__file__).with_name('windows_stack_source_manifest.json'))
        cls.baseline = guard.tree_entries(SOURCE, guard.SOURCE_HEAD)
        cls.allowed = dict(cls.baseline)
        cls.allowed[guard.ADAPTER_PATH] = ('040000', 'tree', cls.manifest['adapter_tree'])
        for path, item in cls.manifest['reviewed_changes'].items():
            cls.allowed[path] = (item['mode'], item['type'], item['oid'])
        for path in cls.manifest['prototype_maintenance_paths']:
            cls.allowed[path] = ('100644', 'blob', '1' * 40)

    def reject_changed(self, path):
        changed = dict(self.allowed)
        changed[path] = ('100644', 'blob', '0' * 40)
        with self.assertRaisesRegex(ValueError, 'unrecognized source change'):
            guard.validate(self.baseline, changed, self.manifest)

    def test_positive_exact_reviewed_pair_and_adapter(self):
        proof = guard.validate(self.baseline, self.allowed, self.manifest)
        self.assertEqual(proof['adapter_reviewed_head'], guard.ADAPTER_HEAD)
        self.assertFalse(proof['historical_attempt_1_av_resolved'])

    def test_positive_actual_committed_head(self):
        guard.validate(self.baseline, guard.tree_entries(ROOT, 'HEAD'), self.manifest)

    def test_manifest_windows_crlf_checkout(self):
        raw = guard.git(ROOT, 'show', 'HEAD:tools/windows_stack_source_manifest.json')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest.json'
            for content in (raw, raw.replace(b'\n', b'\r\n')):
                path.write_bytes(content)
                self.assertEqual(guard.load_manifest(path), self.manifest)

    def test_manifest_content_tamper_rejected(self):
        raw = guard.git(ROOT, 'show', 'HEAD:tools/windows_stack_source_manifest.json')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest.json'
            path.write_bytes(raw.replace(b'"format"', b'"tamper"'))
            with self.assertRaisesRegex(ValueError, 'checkout differs'):
                guard.load_manifest(path)

    def test_negative_production_sentinel(self):
        self.reject_changed('claw/src/codegen/llvm.cpp')

    def test_negative_all_original_native_runtime_and_stack_inputs(self):
        protected = [p for p in self.baseline if (
            p.startswith(('claw/src/', 'claw/runtime/', 'claw/tests/native/', 'tests/', 'runtime/'))
            or p in ('claw/CMakeLists.txt', 'CMakeLists.txt', '.github/workflows/backend-stack-diagnostic.yml'))
            and p not in self.manifest['reviewed_changes']]
        self.assertTrue(any('revise_int16_range.cat' in p for p in protected))
        self.assertTrue(any('run_fixed_alloca_regression.py' in p for p in protected))
        self.assertTrue(any('probe_windows_stack.py' in p for p in protected))
        self.assertTrue(any('windows_stack_status_probe.c' in p for p in protected))
        for path in protected:
            with self.subTest(path=path):
                self.reject_changed(path)

    def test_negative_unknown_paths(self):
        for path in ('claw/src/sentinel.cpp', 'claw/tests/native/sentinel.py',
                     'claw/experimental/other/sentinel.cpp', 'tools/sentinel.py',
                     '.github/workflows/sentinel.yml', 'changelog/sentinel.md'):
            with self.subTest(path=path):
                self.reject_changed(path)

    def test_negative_adapter_tree_not_reviewed(self):
        changed = dict(self.allowed)
        changed[guard.ADAPTER_PATH] = ('040000', 'tree', '0' * 40)
        with self.assertRaisesRegex(ValueError, 'claw/experimental/llvm_cpp'):
            guard.validate(self.baseline, changed, self.manifest)

    def test_negative_pair_files_are_not_blanket_exceptions(self):
        for path in self.manifest['reviewed_changes']:
            with self.subTest(path=path):
                self.reject_changed(path)

    def test_negative_deleted_or_mode_changed_production_file(self):
        path = 'claw/src/codegen/llvm.cpp'
        for entry in (None, ('100755', 'blob', self.allowed[path][2])):
            changed = dict(self.allowed)
            if entry is None:
                del changed[path]
            else:
                changed[path] = entry
            with self.assertRaises(ValueError):
                guard.validate(self.baseline, changed, self.manifest)

    def test_negative_maintenance_symlink(self):
        changed = dict(self.allowed)
        changed['tools/windows_stack_source_guard.py'] = ('120000', 'blob', '0' * 40)
        with self.assertRaisesRegex(ValueError, 'non-regular'):
            guard.validate(self.baseline, changed, self.manifest)

    def test_negative_missing_maintenance_path(self):
        for path in self.manifest['prototype_maintenance_paths']:
            changed = dict(self.allowed)
            del changed[path]
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'missing/non-regular'):
                guard.validate(self.baseline, changed, self.manifest)

    def test_exact_native_harness_invocation_only(self):
        path = 'claw/tests/native/run_native_tests.sh'
        old = guard.git(SOURCE, 'show', guard.SOURCE_HEAD + ':' + path)
        oid = self.manifest['reviewed_changes'][path]['oid']
        current = guard.git(ROOT, 'cat-file', 'blob', oid)
        addition = b'CLAW_EXE="$CLAW_EXE" python3 "$SCRIPT_DIR/run_pair_i32_roundtrip.py"\n'
        self.assertEqual(current.count(addition), 1)
        self.assertEqual(current.replace(addition, b''), old)

    def test_default_off_and_no_pair_adapter_flag(self):
        cmake = guard.git(SOURCE, 'show', guard.SOURCE_HEAD + ':claw/CMakeLists.txt')
        self.assertRegex(cmake.decode(), r'option\(CLAW_ENABLE_LLVM_CPP_POC[^\n]*OFF\)')
        driver = guard.git(ROOT, 'show', 'HEAD:claw/experimental/llvm_cpp/driver.cpp')
        self.assertNotIn(b'--pair-i32', driver)


if __name__ == '__main__':
    unittest.main(verbosity=2)
