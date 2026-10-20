#!/usr/bin/env python3
"""Fail closed at the actual PoC tuple boundary, never at CLI usage parsing.

There is no tuple lowering flag. Exercise default/local identity OFF and ON,
and both identity modes with every currently available lowering feature flag.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

FEATURES = ('--runtime-i32', '--arithmetic-i32', '--strings')
MODES = (('default', ()), ('local-binding-keys', ('--local-binding-keys',)),
         ('all-features-legacy-keys', FEATURES),
         ('all-features-local-keys', (*FEATURES, '--local-binding-keys')))
SOURCE = 'fn make(x: Int32) -> (Int32,) { (x,) }\nfn take(p: (Int32,)) -> Int32 { p.0 }\nfn main() -> Int32 { val p = make(42) return take(p) }\n'
DIAGNOSTIC = "error[llvm-cpp-poc]: LLVM C++ PoC unsupported scalar type 'Tuple[Int32]'"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--emitter', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    root = args.artifacts.resolve(); root.mkdir(parents=True, exist_ok=True)
    result = dict(schema='cat-singleton-fail-closed/v1', status='RUNNING', cases=[],
                  flag_policy='No tuple lowering flag exists; existing local-binding identity OFF/ON and all existing feature flags must still reject.',
                  limitations='Rejecting an unsupported aggregate is not proof of malformed aggregate LIR or slot validation.')
    def save():
        (root/'evidence.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf8')
    try:
        source = args.fixture.resolve()
        require(source.read_text(encoding='utf8') == SOURCE, 'Not the exact requested production singleton probe')
        result['source_sha256'] = digest(source)
        for mode, flags in MODES:
            for existing in (False, True):
                case = root/mode/('sentinels' if existing else 'absent'); case.mkdir(parents=True, exist_ok=True)
                obj, ir = case/'program.o', case/'program.ll'
                sentinels = {obj: b'prior object\x00singleton\xff\n', ir: b'; prior IR\nsentinel\n'}
                for path, sentinel in sentinels.items():
                    if existing:
                        path.write_bytes(sentinel)
                    elif path.exists():
                        path.unlink()  # only artifacts owned by this exact test case
                before = {p.name: digest(p) for p in sentinels} if existing else None
                argv = list(map(str, [args.emitter.resolve(), source, '-o', obj, '--emit-ir', ir, *flags]))
                p = subprocess.run(argv, capture_output=True, timeout=120)
                row = dict(mode=mode, preexisting=existing, argv=argv, exit=p.returncode,
                           stdout_hex=p.stdout.hex(), stderr_hex=p.stderr.hex(), before_sha256=before)
                result['cases'].append(row)
                (case/'stdout.txt').write_bytes(p.stdout); (case/'stderr.txt').write_bytes(p.stderr); save()
                require(p.returncode == 1 and p.stdout == b'', f'{mode}: expected adapter rejection, not crash/success')
                require(p.stderr.decode('utf8').replace('\r\n', '\n').strip() == DIAGNOSTIC,
                        f'{mode}: not the expected tuple rejection: {p.stderr!r}')
                if existing:
                    require(all(path.is_file() and path.read_bytes() == sentinel for path, sentinel in sentinels.items()), f'{mode}: stale object/IR overwritten or removed')
                    row['after_sha256'] = {p.name: digest(p) for p in sentinels}
                    require(row['after_sha256'] == before, f'{mode}: sentinel digest changed')
                else:
                    require(not obj.exists() and not ir.exists(), f'{mode}: newly published object or IR')
                    row['after_artifacts'] = []
                row['status'] = 'PASS'; save()
        result['status'] = 'PASS'; save()
        print('PASS singleton PoC fail-closed: 4 existing flag modes x absent/sentinel object+IR; exact unsupported tuple diagnostic, no publication/overwrite.')
        return 0
    except Exception as exc:
        result.update(status='FAIL', error=str(exc)); save()
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
