#!/usr/bin/env python3
"""Test-only numeric pair layout proof; no adapter codegen or full-suite execution."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = 'c5d901643f50a212800916f5a8d084836b03d2b8'
SOURCE = ROOT / 'claw/tests/native/fixtures/revise_pair_i32_roundtrip.cat'
RECORDS = []


def need(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(argv, expected=0):
    argv = [str(x) for x in argv]
    result = subprocess.run(argv, cwd=ROOT, capture_output=True, timeout=180)
    RECORDS.append({'argv': argv, 'exit': result.returncode,
                    'stdout': result.stdout.decode(errors='replace'),
                    'stderr': result.stderr.decode(errors='replace')})
    need(result.returncode == expected, f'expected exit {expected}: {RECORDS[-1]}')
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect_ir(text, label, effective=False):
    triple = re.search(r'^target triple = "([^"]+)"', text, re.M)
    layout = re.search(r'^target datalayout = "([^"]+)"', text, re.M)
    need(triple is not None and (layout is not None or not effective),
         f'{label}: missing actual triple/effective DataLayout')
    pair, stem = r'\{ i32, i32 \}', re.escape(SOURCE.stem)
    patterns = [
        rf'define internal {pair} @"{stem}::make"\(i32 [^,]+, i32 [^)]+\)',
        rf'define internal i32 @"{stem}::take"\({pair} [^)]+\)',
        rf'call {pair} @"{stem}::make"\(i32 40, i32 2\)',
        rf'call i32 @"{stem}::take"\({pair} [^)]+\)',
    ]
    for pattern in patterns:
        need(re.search(pattern, text) is not None, f'{label}: missing Direct signature/call {pattern}')
    geps = re.findall(r'getelementptr inbounds \{ i32, i32 \}, ptr [^,\n]+, i(?:32|64) 0, i32 ([01])\b', text)
    need(set(geps) == {'0', '1'}, f'{label}: missing actual pair field GEP 0/1')
    return {'triple': triple.group(1), 'datalayout': layout.group(1) if layout else None,
            'direct_internal_signatures_and_calls': True, 'field_gep_indices': sorted(set(geps))}


def main(args, evidence):
    compiler, probe = Path(args.compiler).resolve(), Path(args.probe).resolve()
    clang = shutil.which(args.clang)
    need(compiler.is_file() and probe.is_file() and clang, 'missing compiler/probe/native Clang')
    head = run(['git', 'rev-parse', 'HEAD']).stdout.decode().strip()
    need(head == args.expected_head, 'not exact expected test head')
    # Both native CI jobs require full history: prove the audited production,
    # runtime, adapter and fixture are byte-identical to the exact requested base.
    protected = ['claw/src', 'claw/runtime', 'claw/CMakeLists.txt',
                 'claw/experimental/llvm_cpp', str(SOURCE.relative_to(ROOT))]
    run(['git', 'diff', '--exit-code', BASE, head, '--', *protected])
    need(not run(['git', 'status', '--porcelain', '--', *protected]).stdout,
         'production/runtime/adapter/fixture is dirty')
    source_sha = digest(SOURCE)
    evidence.update({'head': head, 'audited_base': BASE, 'source_sha256': source_sha,
                     'compiler_sha256': digest(compiler), 'probe_sha256': digest(probe),
                     'platform': os.name, 'scope': 'numeric pair layout only; adapter closed'})
    run([clang, '--version'])
    version = run([probe, '--sdk-version']).stdout.decode()
    need(re.search(r'LLVM version 23\.1\.3(?:\s|$)', version) is not None
         and 'LLVM_VERSION_STRING=23.1.3' in version, 'linked LLVM/header version mismatch')
    for stage in ('check', 'oir', 'lir', 'llvm'):
        (args.artifacts / f'{stage}.txt').write_bytes(run([compiler, stage, SOURCE]).stdout)
    lir = (args.artifacts / 'lir.txt').read_text()
    for fragment in ('Tuple[Int32, Int32] [direct]', 'store_field %t0.0',
                     'store_field %t0.1', 'field p.0', 'field p.1',
                     'call.direct make(40, 2)', 'call.direct take(p)'):
        need(fragment in lir, 'missing production LIR: ' + fragment)
    raw = inspect_ir((args.artifacts / 'llvm.txt').read_text(), 'raw')
    effective_path = args.artifacts / 'llvm-effective.ll'
    run([clang, '-S', '-emit-llvm', '-O0', '-x', 'ir', args.artifacts / 'llvm.txt', '-o', effective_path])
    effective = inspect_ir(effective_path.read_text(), 'effective', effective=True)
    def identity(triple):
        parts = triple.split('-')
        return (parts[0], *parts[2:])
    need(identity(raw['triple']) == identity(effective['triple']), 'Clang changed target identity')
    result = run([probe, SOURCE, effective['triple'], effective['datalayout']])
    (args.artifacts / 'probe.json').write_bytes(result.stdout)
    metrics = json.loads(result.stdout)
    need(metrics.get('validated') is True, 'probe did not validate')
    # Deliberately incompatible i32 ABI alignment must fail, not silently normalize.
    bad_layout = effective['datalayout'] + '-i32:64:64'
    negative = run([probe, SOURCE, effective['triple'], bad_layout], expected=1)
    need(b'numeric mismatch' in negative.stderr and not negative.stdout,
         'bad-layout control did not fail closed')
    need(digest(SOURCE) == source_sha, 'fixture changed during probe')
    need(run(['git', 'rev-parse', 'HEAD']).stdout.decode().strip() == head, 'HEAD changed during probe')
    evidence.update({'status': 'PASS', 'raw_llvm': raw, 'effective_llvm': effective,
                     'probe': metrics, 'bad_i32_alignment_control': 'REJECTED'})
    print(json.dumps({'status': 'PASS', 'head': head, 'probe': metrics}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--expected-head', required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    args.artifacts = args.artifacts.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    need(not any(args.artifacts.iterdir()), 'refusing to overwrite evidence; use a fresh directory')
    evidence = {'status': 'FAIL', 'commands': RECORDS}
    try:
        main(args, evidence)
    except Exception as error:
        evidence['error'] = str(error)
        raise
    finally:
        (args.artifacts / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
