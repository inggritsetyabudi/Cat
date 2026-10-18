#!/usr/bin/env python3
from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path

from feature_status_shell_checks import stdout_check_errors

ROOT = Path(__file__).resolve().parents[3]
NATIVE = 'claw/tests/native/run_native_tests.sh'
PRIMARY = 'if [[ "$fixed_array_stdout" != "$expected_fixed_array_stdout" ]]; then'
SANITIZED = 'if [[ "$fixed_array_sanitized_stdout" != "$expected_fixed_array_stdout" ]]; then'


class TestShellStdoutEvidence(unittest.TestCase):
    def repo(self):
        root = Path(tempfile.mkdtemp(prefix='g2-stdout-mutation-'))
        self.addCleanup(shutil.rmtree, root)
        for directory in ('cat-northstar', 'claw/tests', 'claw/src', 'changelog'):
            shutil.copytree(ROOT / directory, root / directory,
                            ignore=shutil.ignore_patterns('artifacts', '__pycache__'))
        shutil.copy(ROOT / 'AGENTS.md', root / 'AGENTS.md')
        return root

    def test_exact_original_both_stdout_assertions_disabled(self):
        from validate_feature_status import validate
        root = self.repo()
        self.assertEqual(validate(root), [])
        path = root / NATIVE
        text = path.read_text(encoding='utf-8')
        self.assertEqual(text.count(PRIMARY), 1)
        self.assertEqual(text.count(SANITIZED), 1)
        path.write_text(text.replace(PRIMARY, 'if false; then').replace(SANITIZED, 'if false; then'), encoding='utf-8')
        errors = validate(root)
        self.assertTrue(any('stdout comparison must occur exactly once' in e for e in errors), errors)
        self.assertTrue(any(SANITIZED in e for e in errors), errors)
        self.assertTrue(any(PRIMARY in e for e in errors), errors)

    def test_sanitizer_comparison_alone_cannot_be_disabled(self):
        from validate_feature_status import validate
        root = self.repo()
        path = root / NATIVE
        path.write_text(path.read_text(encoding='utf-8').replace(SANITIZED, 'if false; then'), encoding='utf-8')
        self.assertTrue(any(SANITIZED in e for e in validate(root)))

    def test_expected_output_declaration_is_not_an_assertion(self):
        from validate_feature_status import validate
        root = self.repo()
        path = root / 'cat-northstar/feature-status-atoms.json'
        manifest = json.loads(path.read_text(encoding='utf-8'))
        assertion = manifest['G2-PRD-2-ARRAY-I32']['assertions'][0]
        assertion['text'] = next(line for line in (root / NATIVE).read_text(encoding='utf-8').splitlines()
                                 if line.startswith('expected_fixed_array_stdout='))
        assertion.pop('checks', None)
        path.write_text(json.dumps(manifest), encoding='utf-8')
        self.assertTrue(any('variable declaration is not' in e for e in validate(root)))

    def block(self):
        return ['fixed_array_stdout="$(normalize_stdout "$output")"', PRIMARY,
                '  echo "unexpected" >&2', '  exit 1', 'fi']

    def test_live_comparison_passes(self):
        self.assertEqual(stdout_check_errors(self.block(), PRIMARY), [])

    def test_commented_or_heredoc_comparison_is_not_live(self):
        for lines in ([('# ' + line) for line in self.block()],
                      ["cat <<'PROOF'"] + self.block() + ['PROOF']):
            self.assertTrue(stdout_check_errors(lines, PRIMARY))

    def test_false_wrapper_and_uncalled_function_fail_closed(self):
        for start, end in [('if false; then', 'fi'), ('unused_proof() {', '}'),
                           ('while false; do', 'done'), ('! {', '}'), ('! (', ')'),
                           ('! {', '} || true'), ('false && {', '}'), ('true || (', ')'),
                           ('if\nfalse; then', 'fi'), ('if\tfalse; then', 'fi'),
                           ('! if false; then', 'fi'), ('function unused\n{', '}')]:
            errors = stdout_check_errors(start.splitlines() + self.block() + [end], PRIMARY)
            self.assertTrue(any('control wrapper' in e for e in errors), errors)

    def test_no_fatal_exit_or_conditional_exit_is_rejected(self):
        for replacement in ('exit 0', 'echo "exit 1"', 'if false; then; exit 1; fi'):
            lines = [line.replace('exit 1', replacement) for line in self.block()]
            self.assertTrue(stdout_check_errors(lines, PRIMARY))

    def test_missing_runtime_capture_is_rejected(self):
        self.assertTrue(stdout_check_errors(self.block()[1:], PRIMARY))

    def test_valid_sanitizer_probe_is_allowed(self):
        lines = ['if printf \'int main(void) { return 0; }\\n\' | clang -fsanitize=address -x c - -o "$probe" >/dev/null 2>&1; then']
        self.assertEqual(stdout_check_errors(lines + self.block() + ['fi'], PRIMARY), [])

    def test_comparison_duplicate_or_echoed_is_rejected(self):
        self.assertTrue(stdout_check_errors(self.block() + self.block(), PRIMARY))
        self.assertTrue(stdout_check_errors(['echo \'%s\'' % PRIMARY], PRIMARY))


if __name__ == '__main__':
    unittest.main(verbosity=2)
