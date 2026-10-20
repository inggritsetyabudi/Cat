#!/usr/bin/env python3
"""Test-only first-chance oracle. Never rewrites the original CI assertion.

A debuggee failure is data, NOT the debugger helper's return code. The helper
must finish successfully AND report matching EXIT_PROCESS/GetExitCodeProcess.
All exceptions except the validated OS loader breakpoint remain NOT_HANDLED.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from windows_stack_contract import build_contract, EXPECTED, SOURCE_HEAD, pe

SO, AV, BP = 0xC00000FD, 0xC0000005, 0x80000003
NOT_HANDLED, CONTINUE = 0x80010001, 0x10002

def need(condition, message):
    if not condition:
        raise AssertionError(message)

def match(pattern, text):
    m = re.search(pattern, text, re.I | re.M)
    need(m is not None, 'missing telemetry: ' + pattern)
    return m

def hx(value):
    return int(value, 16)

def fields(line):
    return {k: hx(v) for k, v in re.findall(r'(\w+)=0x([0-9a-f]+)', line, re.I)}

def snapshot(block):
    ctx = fields(match(r'^rip=.*$', block)[0])
    tib = fields(match(r'^teb=.*$', block)[0])
    reservation = fields(match(r'^stack_reservation .*$', block)[0])
    regions = [fields(m[0]) | {'guard': int(m[1])} for m in
               re.finditer(r'^stack_map .*?guard=(\d+)$', block, re.M)]
    need(regions, 'missing stack map')
    bottom, top = reservation['bottom'], reservation['top']
    need(regions[0]['base'] == bottom, 'map does not start at reserve bottom')
    cursor = bottom
    for r in regions:
        need(r['base'] == cursor and r['size'] > 0, 'noncontiguous stack map')
        cursor += r['size']
    need(cursor == top, 'map does not end at stack top')
    need(tib['stack_base'] == top, 'TIB top mismatch')
    return ctx, tib, reservation, regions

def telemetry(log):
    need(not re.search(r'(?:WaitForDebugEvent|ContinueDebugEvent|CreateProcess|GetExitCodeProcess|process_wait|redirect)_error', log), 'debugger API failure')
    blocks = re.split(r'(?=^event seq=)', log, flags=re.M)[1:]
    need(blocks, 'no debug events')
    exceptions = []
    identities = []
    creates = []
    for index, block in enumerate(blocks, 1):
        h = match(r'^event seq=(\d+) type=(\d+) pid=(\d+) tid=(\d+)$', block)
        seq, kind, pid, tid = map(int, h.groups())
        identities.append(pid)
        if kind == 3:
            creates.append((seq, pid, tid))
        need(seq == index, 'noncontiguous event sequence')
        continuations = re.findall(r'^continue seq=(\d+) disposition=0x([0-9a-f]+)$', block, re.M | re.I)
        need(len(continuations) == 1 and int(continuations[0][0]) == seq, 'missing/duplicate continuation')
        disposition = hx(continuations[0][1])
        if kind == 1:
            e = match(r'^exception=0x([0-9a-f]+) first=(\d+) at=0x([0-9a-f]+).*$', block)
            code, first, address = hx(e[1]), int(e[2]), hx(e[3])
            loader = 'loader_breakpoint_continue=1 validated_ntdll=1' in block
            if loader:
                need(not exceptions and code == BP and first == 1 and disposition == CONTINUE, 'invalid loader continuation')
            else:
                need(disposition == NOT_HANDLED, 'debugger swallowed an application exception')
            exceptions.append({'code': code, 'first': first, 'address': address,
                               'loader': loader, 'block': block, 'tid': tid})
        else:
            need(disposition == CONTINUE, 'nonexception continuation differs')
    need(len(creates) == 1 and creates[0][0] == 1, 'missing/duplicate/out-of-order process creation')
    need(set(identities) == {creates[0][1]}, 'unexpected process identity in debug stream')
    need(exceptions and exceptions[0]['loader'], 'missing validated initial loader breakpoint')
    need(exceptions[0]['tid'] == creates[0][2], 'loader not in initial process thread')
    need(sum(e['loader'] for e in exceptions) == 1, 'duplicate loader breakpoint')
    terminal = re.findall(r'^debugger_exit_code=0x([0-9a-f]+)$', log, re.M | re.I)
    need(len(terminal) == 1, 'missing/duplicate EXIT_PROCESS event')
    raw = match(r'^exit_validation raw=0x([0-9a-f]+) debug_event=0x([0-9a-f]+) match=yes$', log)
    need(hx(raw[1]) == hx(raw[2]) == hx(terminal[0]), 'exit status mismatch')
    return exceptions[1:], hx(terminal[0]), exceptions[0], blocks

def classify(log, contract, exe_sha256):
    severe, terminal, loader, blocks = telemetry(log)
    chain = [(e['code'], e['first']) for e in severe]
    base = hx(match(r'^image_base=0x([0-9a-f]+)', log)[1])
    result = {'terminal': terminal, 'chain': chain, 'all_application_exceptions_not_handled': True,
              'exit_event_matches_GetExitCodeProcess': True}
    if not severe and terminal == 0:
        return result | {'classification': 'normal_exit'}
    overflow = [e for e in severe if e['code'] == SO and e['first'] == 1]
    if len(overflow) != 1:
        return result | {'classification': 'reject_no_unique_first_chance_overflow'}
    event = overflow[0]
    try:
        need(severe[0] is event, 'unexplained exception before overflow')
        need(event['address'] == base + contract['chkstk_push_rax_rva'], 'overflow is not at expected chkstk instruction')
        ctx, tib, reserve, regions = snapshot(event['block'])
        initial_ctx, initial_tib, initial_reserve, initial_regions = snapshot(loader['block'])
        bottom, top = reserve['bottom'], reserve['top']
        page = int(match(r'page_size=(\d+)', log)[1])
        need(page == 4096 and top - bottom == contract['stack_reserve'] == 0x200000, 'unexpected reservation/page size')
        need(initial_reserve['bottom'] == bottom and initial_reserve['top'] == top, 'stack reservation changed')
        need(initial_tib['teb'] == tib['teb'] and event['tid'] == loader['tid'], 'overflow not in initial thread')
        need(any(r['guard'] == 1 and r['protect'] & 0x100 for r in initial_regions), 'initial stack has no guard region')
        need(initial_ctx['rsp'] > top - 0x10000, 'initial stack not near top')
        need(bottom < tib['stack_limit'] <= ctx['rsp'] <= bottom + 5 * page, 'RSP not at exhausted stack boundary')
        need(top - 0x10000 < ctx['rbp'] < top and ctx['rbp'] - ctx['rsp'] > 0x1f0000, 'not repeated same-frame growth')
        need(initial_ctx['rsp'] - ctx['rsp'] > 0x1f0000, 'no large observed stack descent')
        need(ctx['rip'] == event['address'] and ctx['rax'] == 16, 'wrong instruction context/allocation size')
        params = fields(match(r'^exception=.*$', event['block'])[0])
        need(params.get('p0') == 1 and params.get('p1') == ctx['rsp'] - 8, 'fault not chkstk push at stack boundary')
        module = fields(match(r'^exception_module .*$', event['block'])[0])
        need(module['base'] == base and module['rva'] == contract['chkstk_push_rax_rva'] and module['type'] == 0x1000000, 'fault outside baseline image')
        instructions = match(r'^instruction_bytes=([0-9a-f]+)$', event['block'])[1].lower()
        need(instructions == contract['chkstk_bytes_from_push_rax'].lower(), 'runtime chkstk bytes differ from binary')
        words = fields(match(r'^stack_words .*$', event['block'])[0])
        need(words['w1'] - base in contract['allocation_return_rvas'], 'caller not a verified leaking loop callsite')
        fault = fields(match(r'^region fault .*$', event['block'])[0])
        need(fault['allocation'] == bottom and bottom <= params['p1'] <= bottom + 5 * page, 'fault not in same low stack reservation')
        need(regions[0]['state'] == 0x2000 and regions[0]['size'] == page and tib['stack_limit'] <= bottom + 4 * page, 'low reservation/commit limit is not exhausted')
        need(all(r['guard'] == bool(r['protect'] & 0x100) for r in regions), 'inconsistent guard metadata')
        need(all(e['tid'] == event['tid'] for e in severe), 'unexpected exception thread')
        if chain == [(SO, 1), (SO, 0)] and terminal == SO:
            need(severe[1]['address'] == event['address'], 'second-chance overflow address changed')
            classification = 'verified_stack_growth_overflow'
        elif chain == [(SO, 1), (AV, 1), (AV, 0)] and terminal == AV:
            # Diagnose but NEVER silently promote secondary AV to a normal pass.
            for av in severe[1:]:
                ac, at, ar, am = snapshot(av['block'])
                ap = fields(match(r'^exception=.*$', av['block'])[0])
                af = fields(match(r'^region fault .*$', av['block'])[0])
                need(ar['bottom'] == bottom and ar['top'] == top, 'secondary AV changed stack allocation')
                need(bottom <= ap.get('p1', 0) < bottom + 5 * page and af['allocation'] == bottom, 'secondary AV is unrelated to exhausted stack')
                need(af['protect'] & 0x100 or af['protect'] & 1 or af['state'] == 0x2000, 'secondary fault not inaccessible/guard low stack')
            classification = 'verified_stack_growth_then_secondary_AV_BLOCKING'
        else:
            raise AssertionError('unexplained exception chain/terminal')
        need(exe_sha256 == contract['exe_sha256'] == EXPECTED['baseline-O0'], 'binary hash is not immutable baseline')
        return result | {'classification': classification, 'evidence': {
            'fault_rva': event['address'] - base, 'caller_return_rva': words['w1'] - base,
            'rsp_from_bottom': ctx['rsp'] - bottom, 'rbp_from_top': top - ctx['rbp'],
            'initial_guard_regions': initial_regions, 'overflow_regions': regions,
            'stack_growth_bytes': initial_ctx['rsp'] - ctx['rsp']}}
    except AssertionError as exc:
        return result | {'classification': 'reject_not_verified_stack_growth', 'reason': str(exc)}

def native(path):
    return subprocess.check_output(['cygpath', '-w', str(path.resolve())], text=True).strip()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def execute(args, log):
    p = subprocess.run(args, capture_output=True, timeout=90)
    log.write_bytes(p.stdout + p.stderr)
    need(p.returncode == 0, f'helper/command failed: {args!r}: {p.returncode}; see {log}')
    return p

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--debugger', type=Path, required=True)
    p.add_argument('--probe', type=Path, required=True)
    p.add_argument('--controls', type=Path, required=True)
    p.add_argument('--source-root', type=Path, required=True)
    p.add_argument('--repetitions', type=int, default=16)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source_head = subprocess.check_output(['git', '-C', str(args.source_root), 'rev-parse', 'HEAD'], text=True).strip()
    need(source_head == SOURCE_HEAD, 'source snapshot is not exact original PR36 head')
    sys.path.insert(0, str(args.source_root / 'claw/tests/native'))
    from run_fixed_alloca_regression import EXPECTED_INT16
    expected_stdout = EXPECTED_INT16.replace('\n', '\r\n').encode('utf-8')
    pe_settings = set()
    for label, expected in EXPECTED.items():
        exe = args.root / label.split('-')[0] / (label + '.exe')
        need(digest(exe) == expected, 'immutable artifact hash mismatch: ' + label)
        _, reserve, commit, _ = pe(exe)
        pe_settings.add((reserve, commit))
    need(pe_settings == {(0x200000, 0x1000)}, 'PE stack reserve/commit differ or changed')
    baseline = args.root / 'baseline/baseline-O0.exe'
    dis = subprocess.check_output(['objdump', '-w', '-d', str(baseline)], text=True)
    (args.output / 'baseline-O0-disassembly.txt').write_text(dis)
    contract = build_contract(baseline, dis)
    (args.output / 'baseline-contract.json').write_text(json.dumps(contract, indent=2) + '\n')
    records, failures = [], []
    cases = [(f'{variant}-{opt}', args.root / variant / f'{variant}-{opt}.exe', args.repetitions if (variant, opt) == ('baseline', 'O0') else 4)
             for variant in ('baseline', 'fixed') for opt in ('O0', 'O2')]
    cases += [(name, args.controls / (name + '.exe'), 4) for name in ('unrelated-av', 'synthetic-so')]
    # Paired launches record observations, not a claim of identical timing.
    for label, exe, repetitions in cases:
        expected_hash = EXPECTED.get(label, digest(exe))
        for i in range(repetitions):
            prefix = args.output / f'{label}-{i:02d}'
            record = {'case': label, 'iteration': i, 'exe_sha256': digest(exe)}
            try:
                need(record['exe_sha256'] == expected_hash, 'binary changed before launch')
                status = prefix.with_suffix('.native.status')
                execute([str(args.probe), native(exe), native(prefix.with_suffix('.native.stdout')), native(prefix.with_suffix('.native.stderr')), native(status)], prefix.with_suffix('.native.helper.log'))
                raw = status.read_text().strip()
                m = re.fullmatch(r'raw_exit_code=0x([0-9a-fA-F]+) decimal=(\d+)', raw)
                need(m is not None and hx(m[1]) == int(m[2]), 'invalid native raw status')
                record['native_terminal'] = hx(m[1])
                execute([str(args.debugger), native(exe), native(prefix.with_suffix('.debug.stdout')), native(prefix.with_suffix('.debug.stderr'))], prefix.with_suffix('.events.log'))
                need(digest(exe) == expected_hash, 'binary changed during paired launch')
                log = prefix.with_suffix('.events.log').read_text()
                image_path = match(r'^image_base=.* path="([^"]+)"$', log)[1]
                need(image_path.removeprefix('\\\\?\\').lower() == native(exe).lower(), 'debugged image path differs from requested executable')
                result = classify(log, contract, expected_hash)
                record.update(result)
                if label == 'baseline-O0':
                    need(result['classification'] in ('verified_stack_growth_overflow', 'verified_stack_growth_then_secondary_AV_BLOCKING'), 'baseline lacks verified stack-growth SO')
                    need(record['native_terminal'] in (SO, AV), 'unexpected uninstrumented baseline outcome (diagnostic only)')
                    need(classify(log, contract, '0'*64)['classification'].startswith('reject_'), 'wrong hash falsely accepted with authentic overflow telemetry')
                elif label in ('unrelated-av', 'synthetic-so'):
                    need(result['classification'].startswith('reject_'), 'negative control falsely accepted')
                    semantic = classify(log, contract, EXPECTED['baseline-O0'])
                    need(semantic['classification'].startswith('reject_'), 'negative accepted when hash check deliberately bypassed')
                    record['negative_with_baseline_hash'] = semantic
                    if label == 'unrelated-av':
                        need(record['native_terminal'] == result['terminal'] == AV and result['chain'] == [(AV, 1), (AV, 0)], 'unrelated AV outcome swallowed/changed')
                    else:
                        need(record['native_terminal'] == result['terminal'] == SO and result['chain'] == [(SO, 1), (SO, 0)], 'synthetic SO outcome swallowed/changed')
                else:
                    need(result['classification'] == 'normal_exit' and record['native_terminal'] == 0, 'normal variant failed/overflowed')
                    for mode in ('native', 'debug'):
                        need(prefix.with_suffix(f'.{mode}.stderr').read_bytes() == b'', 'normal variant stderr not empty')
                        need(prefix.with_suffix(f'.{mode}.stdout').read_bytes() == expected_stdout, 'normal output differs from exact independent fixture bytes')
                record['native_debug_terminal_equal'] = record['native_terminal'] == result['terminal']
                record['ok'] = True
            except Exception as exc:
                record['ok'] = False
                record['error'] = str(exc)
                failures.append(f'{label}/{i}: {exc}')
            records.append(record)
            with (args.output / 'records.jsonl').open('a') as stream:
                stream.write(json.dumps(record) + '\n')
            print(json.dumps(record), flush=True)
    summary = {'ok': not failures, 'test_only': True, 'source_head': source_head,
               'equal_pe_stack_reserve_commit': sorted(pe_settings),
               'original_assertion_changed': False, 'original_CI_unresolved': True,
               'timing_equivalence_proven': False, 'records': records, 'failures': failures,
               'classifications': dict(Counter(r.get('classification', 'telemetry_failure') for r in records))}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    need(not failures, '; '.join(failures))

if __name__ == '__main__':
    main()
