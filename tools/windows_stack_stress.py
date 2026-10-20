#!/usr/bin/env python3
"""Diagnostic only: repeat original binary with original probe and debugger."""
import collections
import hashlib
import json
import subprocess
from pathlib import Path

root = Path.cwd()
evidence = root / 'evidence'
original = root / 'original'
def native(path):
    return subprocess.check_output(['cygpath', '-w', str(path)], text=True).strip()
probe = str(original / 'windows_stack_status_probe.exe')
exe = native(original / 'baseline/baseline-O0.exe')
records = []
counts = collections.Counter()
for i in range(128):
    prefix = evidence / f'stress-{i:03d}'
    out, err, status = [prefix.with_suffix(s) for s in ('.stdout', '.stderr', '.status')]
    r = subprocess.run([probe, exe, native(out), native(err), native(status)], text=True, capture_output=True, timeout=30)
    raw = status.read_text().strip() if status.exists() else 'missing'
    row = {'iteration': i, 'probe_returncode': r.returncode, 'status': raw,
           'stdout_sha256': hashlib.sha256(out.read_bytes()).hexdigest() if out.exists() else None,
           'stderr_bytes': err.stat().st_size if err.exists() else None}
    records.append(row); counts[raw] += 1
    if i % 16 == 0: print(json.dumps({'progress': i, 'counts': dict(counts)}), flush=True)
(evidence / 'stress-summary.json').write_text(json.dumps({'counts': dict(counts), 'records': records}, indent=2)+'\n')
# Debugger is observational. Compare exception address and terminal status, not
# just final OS code, so an AV cannot be reclassified by assumption.
counts = collections.Counter()
for i in range(64):
    p = evidence / f'debug-stress-{i:03d}.txt'
    r = subprocess.run([str(evidence / 'debug.exe'), exe], text=True, capture_output=True, timeout=30)
    p.write_text(r.stdout + r.stderr)
    final = [s for s in r.stdout.splitlines() if s.startswith('debugger_exit_code=')]
    counts[';'.join(final)] += 1
    if i % 16 == 0: print(json.dumps({'debug_progress': i, 'counts': dict(counts)}), flush=True)
(evidence / 'debug-stress-summary.json').write_text(json.dumps(dict(counts), indent=2)+'\n')
print(json.dumps({'debug_final': dict(counts)}), flush=True)
