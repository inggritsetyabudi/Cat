#!/usr/bin/env python3
"""Production-only native ABI/cleanup evidence. No String support in the adapter."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(HERE.parent))
from lld.test_lld import isolate
from test_poc import verify_target

KEYS = ('allocations', 'frees', 'outstanding', 'untracked_frees')
LAYOUT = b'Str size=16 align=8 ptr=0 len=8\nString size=24 align=8 ptr=0 len=8 cap=16\n'
SAN_ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(argv, env=None):
    argv = list(map(str, argv))
    p = subprocess.run(argv, capture_output=True, env=env, timeout=120)
    return {'argv': argv, 'exit': p.returncode, 'stdout_hex': p.stdout.hex(),
            'stderr_hex': p.stderr.hex(), 'stdout': p.stdout.decode('utf8', 'replace'),
            'stderr': p.stderr.decode('utf8', 'replace')}


def good(result):
    require(result['exit'] == 0, f"Command failed: {result}")
    return result


def native_bytes(data):
    return data.replace(b'\n', b'\r\n') if sys.platform == 'win32' else data


def assert_output(result, expected, stderr_empty=True):
    good(result)
    require(bytes.fromhex(result['stdout_hex']) == native_bytes(expected), f'Native bytes mismatch: {result}')
    if stderr_empty:
        require(not result['stderr_hex'], f'Unexpected stderr: {result}')


def counts(result):
    good(result)
    matches = re.findall(r'AUDIT summary allocations=(\d+) frees=(\d+) outstanding=(\d+) untracked_frees=(\d+)', result['stderr'])
    require(len(matches) == 1, f'Missing/duplicate observer summary: {result}')
    return dict(zip(KEYS, map(int, matches[0])))


def assert_balance(ledger, expected=None):
    require(ledger['allocations'] == ledger['frees'] and ledger['outstanding'] == 0 and ledger['untracked_frees'] == 0,
            f'Unbalanced runtime allocation ledger: {ledger}')
    if expected is not None:
        require(ledger['allocations'] == expected, f'Allocation count mismatch: {ledger} expected {expected}')


def ir_facts(text):
    def field(name):
        m = re.search(r'^target ' + name + r' = "([^"]+)"', text, re.M)
        return m.group(1) if m else None
    return {'target_triple': field('triple'), 'data_layout': field('datalayout'),
            'declarations_definitions': re.findall(r'^(?:define|declare) .*$', text, re.M),
            'calls_operands_attributes': [l.strip() for l in text.splitlines() if re.search(r'\bcall\b', l)],
            'attribute_groups': re.findall(r'^attributes #.*$', text, re.M),
            'symbols': sorted(set(a or b for a, b in re.findall(r'@(?:"([^"]+)"|([A-Za-z0-9_.$:-]+))', text)))}


def abi_markers(text, lir, windows):
    parameter = 'ptr %arg.value.addr' if windows else '%claw.slice %arg.value'
    expected = {
        'str_helper': f'define internal %claw.slice @"abi_calls::borrowed_ref"({parameter}) {{',
        'borrowed_helper': f'define internal %claw.slice @"abi_calls::borrowed_view"({parameter}) {{',
        'owned_helper': 'define internal void @"abi_calls::owned_helper"(ptr %ret.slot, ptr %arg.value.addr) {',
        'constructor_return': 'define internal void @"abi_calls::make_owned"(ptr %ret.slot) {',
    }
    for label, declaration in expected.items():
        require(declaration in text.splitlines(), f'{label}: exact ABI definition missing: {declaration}')
    for marker in (
        'lir.fn borrowed_ref(value: ref Str [direct]) -> ref Str [direct]',
        'lir.fn borrowed_view(value: ref Str [direct]) -> ref Str [direct]',
        'lir.fn owned_helper(value: String [indirect]) -> String [indirect]',
        'lir.fn make_owned() -> String [indirect]',
    ):
        require(marker in lir, f'LIR ABI classification missing: {marker}')
    for decl in ('%claw.slice = type { ptr, i64 }', '%claw.buffer = type { ptr, i64, i64 }',
                 'declare ptr @"claw.runtime.string.alloc"(i64)',
                 'declare void @"claw.runtime.string.copy"(ptr, ptr, i64)',
                 'declare void @"claw.runtime.string.free"(ptr)',
                 'declare void @"claw.runtime.println.slice"(ptr)'):
        require(decl in text.splitlines(), f'Exact record/runtime declaration missing: {decl}')
    calls = ir_facts(text)['calls_operands_attributes']
    for function in ('borrowed_ref', 'borrowed_view', 'owned_helper', 'make_owned'):
        selected = [c for c in calls if f'@"abi_calls::{function}"(' in c]
        require(len(selected) == 1, f'Expected one actual caller for {function}: {selected}')
        call = selected[0]
        if function in ('borrowed_ref', 'borrowed_view'):
            require(('(ptr ' if windows else '(%claw.slice ') in call, f'Incorrect Str caller ABI: {call}')
        else:
            require('call void ' in call and '(ptr ' in call, f'Missing result-slot call: {call}')
            require(call.count('ptr ') == (2 if function == 'owned_helper' else 1), f'Owned call slot arity: {call}')
        require(not re.search(r'\b(sret|byval|nonnull|nocapture|signext|zeroext)\b', call), f'Unexpected attributes: {call}')
    return {'definitions': expected, 'calls': calls, 'attributes_policy': 'exact production ABI; no speculative sret/byval/nonnull/nocapture'}


def executable_format(path, windows):
    raw = Path(path).read_bytes()
    if windows:
        require(raw[:2] == b'MZ', 'Expected native PE')
        offset = int.from_bytes(raw[60:64], 'little')
        require(raw[offset:offset+6] == b'PE\0\0\x64\x86', 'Expected PE AMD64')
        require(int.from_bytes(raw[offset+24:offset+26], 'little') == 0x20b, 'Expected PE32+')
        return 'PE32+ AMD64'
    require(raw[:6] == b'\x7fELF\x02\x01' and int.from_bytes(raw[18:20], 'little') == 62, 'Expected ELF64 x86_64')
    return 'ELF64 little-endian x86_64'


def main():
    p = argparse.ArgumentParser()
    for name in ('production', 'emitter', 'artifacts', 'cc', 'profile'):
        p.add_argument('--' + name, required=True)
    p.add_argument('--expected-sha')
    args = p.parse_args()
    windows = sys.platform == 'win32'
    profile = 'windows-x86_64-ucrt-coff' if windows else 'linux-x86_64-elf'
    require(sys.platform in ('linux', 'win32') and args.profile == profile, 'Native Linux or native UCRT Windows only')
    root = Path(args.artifacts).resolve(); root.mkdir(parents=True, exist_ok=True)
    result = {'schema': 'cat-production-string-abi/v1', 'status': 'running', 'profile': profile, 'cases': []}
    def save():
        (root / 'evidence.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf8')
    try:
        sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
        require(not (args.expected_sha or os.getenv('EXPECTED_HEAD_SHA')) or sha == (args.expected_sha or os.getenv('EXPECTED_HEAD_SHA')), 'Not exact requested PR SHA')
        result.update(git_sha=sha, platform=platform.platform(), python=sys.version,
                      tools={'cc_version': good(run([args.cc, '--version'])), 'cc_target': good(run([args.cc, '-dumpmachine']))})
        compiler = Path(args.production).resolve(); emitter = Path(args.emitter).resolve()
        result['binaries'] = {str(f): digest(f) for f in (compiler, emitter)}
        runtime = REPO / 'claw/runtime/native_runtime.c'
        result['runtime_sha256_before'] = digest(runtime)
        manifest = json.loads((HERE / 'expectations.json').read_text(encoding='utf8'))
        result['expectations_sha256'] = digest(HERE / 'expectations.json')
        cc = [args.cc, '-std=gnu11', '-Wno-override-module']
        layout_exe = root / 'layout_oracle.exe'
        result['layout_build'] = good(run([*cc, HERE/'layout_oracle.c', '-o', layout_exe]))
        result['layout'] = good(run([layout_exe])); assert_output(result['layout'], LAYOUT)
        selftest = root/'observer_selftest.exe'
        result['observer_selftest_build'] = good(run([*cc, HERE/'observer_selftest.c', HERE/'observer_runtime.c', '-o', selftest]))
        result['observer_controls'] = {}
        for mode, values in [('normal', (1,1,0,0)), ('leak', (1,0,1,0)), ('untracked', (1,1,0,1))]:
            record = good(run([selftest, mode])); ledger = counts(record)
            assert_output(record, f'observer-selftest:{"ok" if mode == "normal" else mode}\n'.encode(), False)
            require(ledger == dict(zip(KEYS, values)), f'Unexpected observer control: {ledger}')
            if mode == 'normal':
                assert_balance(ledger, 1)
            else:
                try:
                    assert_balance(ledger)
                except RuntimeError:
                    pass
                else:
                    raise RuntimeError('Negative observer control falsely accepted')
            result['observer_controls'][mode] = {'run': record, 'ledger': ledger, 'balanced_validator_rejected': mode != 'normal'}
        # Probe options through compilation AND execution, retaining objective SKIP evidence.
        probe = root/'sanitizer-probe.c'; probe.write_text('int main(void) {return 0;}\n', encoding='ascii')
        result['sanitizer_probes'] = {}; supported = []
        for option in ('address', 'undefined', 'leak', 'address,undefined,leak'):
            exe = root/('probe-' + option.replace(',', '-') + '.exe')
            build = run([*cc, '-fsanitize=' + option, probe, '-o', exe])
            executed = run([exe], SAN_ENV) if build['exit'] == 0 else None
            ok = executed is not None and executed['exit'] == 0 and not executed['stderr_hex']
            result['sanitizer_probes'][option] = {'build': build, 'run': executed, 'status': 'PASS' if ok else 'SKIP', 'meaning': 'availability probe, not corpus safety proof'}
            if ok: supported.append(option)
        require(windows or 'address,undefined,leak' in supported, 'Mandatory Linux target ASan/UBSan/LSan unavailable')
        # Use combined on Linux; on Windows exercise each actually available sanitizer.
        sanitizer_options = ['address,undefined,leak'] if not windows else [s for s in supported if ',' not in s]
        result['sanitizer_corpus_options'] = sanitizer_options
        readobj = shutil.which('llvm-readobj')
        isolated, env, audit = isolate(emitter, 'String rejection emitter', root/'emitter-isolated', readobj)
        env.update(CC='/__missing_compiler__', CXX='/__missing_compiler__', CLAW_NATIVE_CLANG='/__missing_compiler__', NoDefaultCurrentDirectoryInExePath='1')
        for name in ('clang', 'clang++', 'cc', 'gcc', 'g++', 'cl', 'clang-23'):
            require(shutil.which(name, path=env['PATH']) is None, f'Compiler accessible during PoC rejection: {name}')
        result['poc_isolation'] = audit
        scalar = root/'scalar_control.cat'; scalar.write_text('fn main() -> Int32 { return 7 }\n', encoding='ascii')
        scalar_obj = root/'scalar_control.o'; scalar_ir = root/'scalar_control.ll'
        result['scalar_control'] = good(run([isolated, scalar, '-o', scalar_obj, '--emit-ir', scalar_ir], env))
        result['scalar_target'] = verify_target(scalar_obj, scalar_ir)
        for expected in manifest['cases']:
            name = expected['name']; src = REPO/expected['source']; require(src.is_file(), f'Missing required corpus: {src}')
            directory = root/name; directory.mkdir(exist_ok=True)
            (directory/src.name).write_bytes(src.read_bytes())
            case = {'name': name, 'source': expected['source'], 'source_sha256': digest(src), 'expected': expected, 'stages': {}, 'profiles': {}, 'poc_rejections': {}}
            result['cases'].append(case); save()
            for stage in ('air', 'oir', 'lir', 'llvm'):
                record = good(run([compiler, stage, src])); case['stages'][stage] = record
                (directory/(stage+'.txt')).write_bytes(bytes.fromhex(record['stdout_hex']))
            for level, flags, opt in [('O0', [], '-O0'), ('O2', ['--release'], '-O2')]:
                d = directory/level; d.mkdir(exist_ok=True)
                exe = d/'production.exe'; ll = exe.with_suffix('.ll')
                item = {}; case['profiles'][level] = item
                item['build'] = good(run([compiler, 'build', *flags, src, exe]))
                require(ll.is_file() and exe.is_file(), 'Production build missing artifacts')
                item['production'] = good(run([exe])); assert_output(item['production'], bytes.fromhex(expected['stdout_hex']))
                require(item['production']['exit'] == expected['exit'], 'Unexpected native exit')
                item['executable_format'] = executable_format(exe, windows)
                text = ll.read_text(encoding='utf8'); item['raw_ir'] = ir_facts(text)
                require(item['raw_ir']['target_triple'] == ('x86_64-w64-windows-gnu' if windows else 'x86_64-unknown-linux-gnu'), 'Wrong production triple')
                item['raw_ir']['data_layout_origin'] = 'explicit in Cat IR' if item['raw_ir']['data_layout'] else 'absent in Cat IR; do not infer from C layout'
                # The production driver leaves DataLayout unspecified. Capture the actual native Clang effective module separately.
                effective = d/'clang-effective.ll'
                item['effective_ir_build'] = good(run([args.cc, '-Wno-override-module', '-S', '-emit-llvm', '-O0', '-x', 'ir', ll, '-o', effective]))
                item['effective_clang_ir'] = ir_facts(effective.read_text(encoding='utf8'))
                require(item['effective_clang_ir']['data_layout'] and item['effective_clang_ir']['data_layout'].startswith('e-'), 'Missing effective native Clang DataLayout')
                if name == 'abi_calls':
                    item['abi_oracle'] = abi_markers(text, case['stages']['lir']['stdout'], windows)
                observed = d/'observed.exe'
                item['observer_build'] = good(run([*cc, opt, ll, HERE/'observer_runtime.c', '-o', observed]))
                item['observer'] = good(run([observed])); assert_output(item['observer'], bytes.fromhex(expected['stdout_hex']), False)
                item['allocation_counts'] = counts(item['observer']); assert_balance(item['allocation_counts'], expected['allocations'])
                require(re.fullmatch(r'(?:AUDIT (?:alloc|free) \d+\r?\n)*AUDIT summary allocations=\d+ frees=\d+ outstanding=0 untracked_frees=0\r?\n', item['observer']['stderr']), 'Unexpected observer diagnostic')
                item['sanitizers'] = {}
                for option in sanitizer_options:
                    label = option.replace(',', '-')
                    sanexe = d/('sanitized-' + label + '.exe')
                    record = {'build': good(run([*cc, opt, '-fsanitize='+option, '-fno-omit-frame-pointer', '-g', ll, runtime, '-o', sanexe]))}
                    record['run'] = good(run([sanexe], SAN_ENV)); assert_output(record['run'], bytes.fromhex(expected['stdout_hex']))
                    obsexe = d/('observed-' + label + '.exe')
                    record['observer_build'] = good(run([*cc, opt, '-fsanitize='+option, '-fno-omit-frame-pointer', '-g', ll, HERE/'observer_runtime.c', '-o', obsexe]))
                    record['observer_run'] = good(run([obsexe], SAN_ENV)); assert_output(record['observer_run'], bytes.fromhex(expected['stdout_hex']), False)
                    record['ledger'] = counts(record['observer_run']); assert_balance(record['ledger'], expected['allocations'])
                    require('Sanitizer' not in record['observer_run']['stderr'] and 'runtime error:' not in record['observer_run']['stderr'], 'Sanitizer diagnostic in observer run')
                    item['sanitizers'][option] = record
                save()
            for mode, flags in [('default', []), ('allflags', ['--runtime-i32', '--arithmetic-i32'])]:
                obj = directory/(mode+'.poc.o'); ir = directory/(mode+'.poc.ll')
                for f in (obj, ir): f.unlink(missing_ok=True)
                record = run([isolated, src, '-o', obj, '--emit-ir', ir, *flags], env)
                require(record['exit'] != 0 and 'LLVM C++ PoC unsupported' in record['stderr'], f'Not an explicit adapter rejection: {record}')
                require(not obj.exists() and not ir.exists(), 'Rejected String published object/IR')
                require(not list(directory.glob(mode+'.poc.*.tmp')), 'Rejected String left staged artifacts')
                case['poc_rejections'][mode] = {'run': record, 'object_exists': False, 'ir_exists': False, 'PATH': env['PATH']}
            save()
            print(f"PASS {name}: O0/O2 native + observer {expected['allocations']}/{expected['allocations']}, ABI evidence, PoC rejected both modes", flush=True)
        result['runtime_sha256_after'] = digest(runtime)
        require(result['runtime_sha256_before'] == result['runtime_sha256_after'], 'Production runtime changed')
        result['status'] = 'PASS'
        result['summary'] = {'cases': len(result['cases']), 'profiles': 2, 'poc_rejections': sum(len(c['poc_rejections']) for c in result['cases']), 'string_backend_support_added': False}
        save(); print(json.dumps(result['summary']), flush=True)
    except BaseException as error:
        result['status'] = 'FAIL'; result['error'] = str(error); save(); raise


if __name__ == '__main__':
    main()
