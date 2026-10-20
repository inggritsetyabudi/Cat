#!/usr/bin/env python3
"""Diagnostic experiment, NOT a replacement for the PR36 CI assertions."""
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path.cwd()
EVIDENCE = ROOT / 'evidence'
ORIGINAL = ROOT / 'original'
EXPECTED = {
    'baseline-O0': '3bafd2d9f4e5148f070c94bd62c48316fc452d307b5ed0c57d06bd81a406784b',
    'baseline-O2': '1efece78f12699e17b03e54f51cfb6ab8b012c5c8099753517bd339004541602',
    'fixed-O0': '4ffd6725db64f4184e6dcdcbdfb959fbd4042766491f89e4db44c249ec65a209',
    'fixed-O2': '4c13c128521ad4e214928f959c22b0f035eda9e646b12385838dbe2d99ba9e46',
}

def native(path):
    return subprocess.check_output(['cygpath', '-w', str(path)], text=True).strip()

def classify(log, original_baseline=False):
    events = [(int(c, 16), int(first), int(addr, 16)) for c, first, addr in
              re.findall(r'exception=0x([0-9a-fA-F]+) first=(\d+) at=0x([0-9a-fA-F]+)', log)]
    exits = re.findall(r'debugger_exit_code=0x([0-9a-fA-F]+)', log)
    severe = [e for e in events if e[0] != 0x80000003]
    if len(exits) != 1:
        return 'incomplete_telemetry'
    terminal = int(exits[0], 16)
    if terminal == 0 and not severe:
        return 'normal_exit'
    # A code alone, including a software-raised STATUS_STACK_OVERFLOW, is insufficient.
    base_match = re.search(r'image_base=0x([0-9a-fA-F]+)', log)
    if not original_baseline or not base_match:
        return 'reject_unrelated_exception'
    image_base = int(base_match[1], 16)
    overflow = [e for e in severe if e[0] == 0xC00000FD and e[1] == 1]
    if len(overflow) != 1 or overflow[0][2] - image_base != 0x3CC1:
        return 'reject_missing_expected_stack_overflow'
    # Corroborate actual stack exhaustion with TIB and allocation boundaries,
    # not an exception name. Match the block for the first-chance overflow.
    block = log.split('exception=0xc00000fd first=1', 1)[1].split('exception=', 1)[0]
    ctx = re.search(r'rip=0x[0-9a-f]+ rsp=0x([0-9a-f]+) rbp=0x([0-9a-f]+)', block)
    tib = re.search(r'stack_base=0x([0-9a-f]+) stack_limit=0x([0-9a-f]+)', block)
    reg = re.search(r'region rsp .*?allocation=0x([0-9a-f]+)', block)
    if not (ctx and tib and reg):
        return 'reject_missing_stack_boundaries'
    rsp, rbp = map(lambda s: int(s, 16), ctx.groups())
    top, limit = map(lambda s: int(s, 16), tib.groups())
    bottom = int(reg[1], 16)
    if not (top - bottom == 0x200000 and bottom < limit <= rsp <= bottom + 0x5000
            and top - 0x10000 < rbp < top and top - rsp > 0x1F0000):
        return 'reject_not_exhausted_at_expected_probe'
    chain = [(c, f) for c, f, _ in severe]
    if chain == [(0xC00000FD, 1), (0xC00000FD, 0)] and terminal == 0xC00000FD:
        return 'verified_stack_exhaustion'
    if chain == [(0xC00000FD, 1), (0xC0000005, 1), (0xC0000005, 0)] and terminal == 0xC0000005:
        # Preserve this as a blocking diagnostic, never an ordinary passing exit.
        return 'verified_stack_exhaustion_then_av_BLOCKING'
    return 'reject_unexplained_exception_chain'


def main():
    EVIDENCE.mkdir(exist_ok=True)
    records = []
    for label, expected in EXPECTED.items():
        exe = ORIGINAL / label.split('-')[0] / (label + '.exe')
        actual = hashlib.sha256(exe.read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f'immutable attempt-1 hash mismatch for {label}: {actual}')
    cases = [(label, ORIGINAL / label.split('-')[0] / (label + '.exe'), 16 if label == 'baseline-O0' else 3)
             for label in EXPECTED]
    cases += [('unrelated-AV', EVIDENCE / 'unrelated-av.exe', 2),
              ('synthetic-SO', EVIDENCE / 'synthetic-so.exe', 2)]
    for label, exe, repetitions in cases:
        for i in range(repetitions):
            p = subprocess.run([str(EVIDENCE / 'debug.exe'), native(exe)], capture_output=True, text=True, timeout=75)
            log = p.stdout + p.stderr
            (EVIDENCE / f'firstchance-{label}-{i:02d}.txt').write_text(log)
            outcome = classify(log, original_baseline=label == 'baseline-O0')
            row = {'case': label, 'iteration': i, 'debugger_returncode': p.returncode,
                   'classification': outcome,
                   'exception_chain': re.findall(r'exception=0x[0-9a-f]+ first=\d+', log),
                   'terminal': re.findall(r'debugger_exit_code=0x[0-9a-f]+', log),
                   'sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'argv': [native(exe)]}
            records.append(row)
            print(json.dumps(row), flush=True)
            with (EVIDENCE / 'firstchance-records.jsonl').open('a') as stream:
                stream.write(json.dumps(row) + '\n')
            if p.returncode != 0:
                raise RuntimeError(f'telemetry failed: {label}, see retained log')
            if label == 'baseline-O0':
                assert outcome in ('verified_stack_exhaustion', 'verified_stack_exhaustion_then_av_BLOCKING'), row
            elif label in EXPECTED:
                assert outcome == 'normal_exit', row
            else:
                assert outcome == 'reject_unrelated_exception', row
    # Independent original native probe, no debugger. Preserve raw Windows values.
    for label in EXPECTED:
        exe = ORIGINAL / label.split('-')[0] / (label + '.exe')
        for i in range(4):
            prefix = EVIDENCE / f'native-{label}-{i:02d}'
            args = [str(ORIGINAL / 'windows_stack_status_probe.exe'), native(exe)]
            args += [native(prefix.with_suffix(s)) for s in ('.stdout', '.stderr', '.status')]
            p = subprocess.run(args, capture_output=True, text=True, timeout=30)
            records.append({'case': label, 'iteration': i, 'mode': 'original_native_probe',
                            'probe_returncode': p.returncode,
                            'raw_status': prefix.with_suffix('.status').read_text().strip()})
            assert p.returncode == 0
            raw = prefix.with_suffix('.status').read_text().strip()
            if label != 'baseline-O0':
                assert raw == 'raw_exit_code=0x00000000 decimal=0', (label, raw)
    normal = (EVIDENCE / 'native-baseline-O2-00.stdout').read_bytes()
    for label in ('fixed-O0', 'fixed-O2'):
        for i in range(4):
            assert (EVIDENCE / f'native-{label}-{i:02d}.stdout').read_bytes() == normal
            assert (EVIDENCE / f'native-{label}-{i:02d}.stderr').read_bytes() == b''
    result = {'diagnostic_only': True, 'production_gate_changed': False,
              'av_after_stack_overflow_remains_blocking': True,
              'classifications': dict(collections.Counter(r['classification'] for r in records if 'classification' in r)),
              'records': records}
    (EVIDENCE / 'firstchance-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'records'}), flush=True)

if __name__ == '__main__':
    main()
