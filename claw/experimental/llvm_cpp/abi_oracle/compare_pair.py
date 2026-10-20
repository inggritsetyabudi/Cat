#!/usr/bin/env python3
"""Compare two actually executed native oracle reports, never emulate a target."""
import argparse
import hashlib
import json
from pathlib import Path


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--linux', type=Path, required=True)
    parser.add_argument('--windows', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    a = json.loads(args.linux.read_text(encoding='utf8'))
    b = json.loads(args.windows.read_text(encoding='utf8'))
    require(a['status'] == b['status'] == 'PASS', 'Both native executions must PASS')
    require(a['profile'] == 'linux-x86_64-elf' and b['profile'] == 'windows-x86_64-ucrt-coff', 'Not a native Linux/Windows pair')
    require(a['git_sha'] == b['git_sha'], 'Different source commits')
    require(a['expectations_sha256'] == b['expectations_sha256'], 'Different corpus expectations')
    require(a['runtime_sha256_before'] == a['runtime_sha256_after'] == b['runtime_sha256_before'] == b['runtime_sha256_after'], 'Different or mutated production runtimes')
    require(bytes.fromhex(b['layout']['stdout_hex']) == bytes.fromhex(a['layout']['stdout_hex']).replace(b'\n', b'\r\n'), 'Native C layout oracle output mismatch')
    left = {c['name']: c for c in a['cases']}; right = {c['name']: c for c in b['cases']}
    require(left.keys() == right.keys(), 'Native corpus case sets differ')
    rows = []
    for name, linux in left.items():
        windows = right[name]
        require(linux['source_sha256'] == windows['source_sha256'] and linux['expected'] == windows['expected'], f'{name}: different source/oracle')
        require(set(linux['poc_rejections']) == set(windows['poc_rejections']) == {'default', 'allflags'}, 'Missing negative modes')
        for case in (linux, windows):
            for reject in case['poc_rejections'].values():
                require(reject['run']['exit'] != 0 and not reject['object_exists'] and not reject['ir_exists'], 'String adapter rejection failed')
        for level in ('O0', 'O2'):
            x, y = linux['profiles'][level], windows['profiles'][level]
            require(x['production']['exit'] == y['production']['exit'] == 0, 'Native exit mismatch')
            require(not x['production']['stderr_hex'] and not y['production']['stderr_hex'], 'Unexpected native stderr')
            require(bytes.fromhex(y['production']['stdout_hex']) == bytes.fromhex(x['production']['stdout_hex']).replace(b'\n', b'\r\n'), f'{name}/{level}: raw bytes differ beyond native CRT newline translation')
            require(x['allocation_counts'] == y['allocation_counts'], f'{name}/{level}: native cleanup counts differ')
            for item in (x, y):
                ledger = item['allocation_counts']
                require(ledger['allocations'] == ledger['frees'] == linux['expected']['allocations'] and ledger['outstanding'] == ledger['untracked_frees'] == 0, 'Native cleanup mismatch')
                require(item['effective_clang_ir']['data_layout'], 'Missing effective DataLayout evidence')
            rows.append({'case': name, 'profile': level, 'allocation_counts': x['allocation_counts'],
                         'linux_stdout_hex': x['production']['stdout_hex'], 'windows_stdout_hex': y['production']['stdout_hex']})
    signatures = {}
    for level in ('O0', 'O2'):
        x = left['abi_calls']['profiles'][level]['abi_oracle']['definitions']
        y = right['abi_calls']['profiles'][level]['abi_oracle']['definitions']
        for key in ('str_helper', 'borrowed_helper'):
            require('(%claw.slice %arg.value)' in x[key] and '(ptr %arg.value.addr)' in y[key], 'Missing observed SysV/Win64 parameter difference')
        for key in ('owned_helper', 'constructor_return'):
            require(x[key] == y[key], 'Owned indirect signature unexpectedly differs')
        signatures[level] = {'linux': x, 'windows': y}
    result = {'status': 'PASS', 'git_sha': a['git_sha'], 'cases': len(left), 'native_profile_pairs': len(rows),
              'inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.linux, args.windows)},
              'signatures': signatures, 'cleanup_and_output': rows,
              'sanitizer_probes': {'linux': a['sanitizer_probes'], 'windows': b['sanitizer_probes']},
              'scope': 'Production Cat ABI and observed cleanup only; String adapter remains unsupported. Native observer is not Windows ASan/LSan.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf8')
    print(f"PASS paired native ABI/cleanup: {len(left)} cases, SHA {a['git_sha']}")


if __name__ == '__main__':
    main()
