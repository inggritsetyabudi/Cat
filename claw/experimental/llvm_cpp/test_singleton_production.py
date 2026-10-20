#!/usr/bin/env python3
"""Exact production singleton round-trip evidence; never enables adapter lowering.

Cat's direct aggregate ABI is not a C ABI promise. The layout probe compares
Cat metadata with the effective native Clang and LLVM TargetMachine DataLayouts.
Malformed-LIR acceptance/slot validation is deliberately NOT established here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SOURCE = REPO / 'claw/tests/native/singleton_roundtrip.cat'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def run(argv):
    argv = list(map(str, argv))
    p = subprocess.run(argv, capture_output=True, timeout=120)
    return dict(argv=argv, exit=p.returncode, stdout=p.stdout.decode('utf8', 'replace'),
                stderr=p.stderr.decode('utf8', 'replace'),
                stdout_hex=p.stdout.hex(), stderr_hex=p.stderr.hex())


def good(record):
    require(record['exit'] == 0, f'Command failed: {record}')
    return record


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect_stages(oir, lir):
    for stage, text in [('oir', oir), ('lir', lir)]:
        for signature in (
            f'{stage}.fn make(x: Int32 [scalar]) -> Tuple[Int32] [direct] [abi=claw link=internal symbol=singleton_roundtrip::make ffi=false]',
            f'{stage}.fn take(p: Tuple[Int32] [direct]) -> Int32 [scalar] [abi=claw link=internal symbol=singleton_roundtrip::take ffi=false]',
        ):
            require(signature in text.splitlines(), f'Missing exact internal Cat ABI: {signature}')
        require('store_field %t0.0 <- x : Int32' in text, f'{stage}: missing field-0 initialization')
        require('%t0 = field p.0 : Int32' in text, f'{stage}: missing field-0 projection')
        require('drop ' not in text, f'{stage}: unexpected cleanup')
    for marker in ('alloc stack val %t0 : Tuple[Int32]', 'store_field %t0.0 <- x : Int32 [proven-safe]',
                   'alloc stack val p : Tuple[Int32]', 'store p <- %t0 : Tuple[Int32]',
                   '%t0 = call.direct make(42) : Tuple[Int32]', '%t1 = call.direct take(p) : Int32'):
        require(marker in lir, f'Missing production storage/call marker: {marker}')


def inspect_ir(text, native=True):
    # Check the unoptimized production module, not Clang's optimizer output.
    # In particular, {i32} must not be scalar-coerced as if this were C FFI.
    definitions = re.findall(r'^define .*$', text, re.M)
    for expected in (
        'define internal { i32 } @"singleton_roundtrip::make"(i32 %arg.x) {',
        'define internal i32 @"singleton_roundtrip::take"({ i32 } %arg.p) {',
        'define internal i32 @"singleton_roundtrip::main"() {',
    ):
        require(expected in definitions, f'Missing exact Cat LLVM signature: {expected}')
    require(len(definitions) == (4 if native else 3), 'Unexpected function definition(s)')
    require(('define i32 @main() {' in definitions) == native, 'Wrong native wrapper policy')
    require(not re.search(r'\b(sret|byval|inalloca|preallocated|undef|poison)\b', text), 'Unexpected ABI coercion or uninitialized value')
    require('ret.slot' not in text and 'runtime.' not in text, 'Unexpected hidden result/runtime cleanup')
    lines = text.splitlines()
    allocas = [l.strip() for l in lines if 'alloca { i32 }' in l]
    geps = [l.strip() for l in lines if 'getelementptr' in l]
    require(len(allocas) == 3 and all(l.endswith(', align 4') for l in allocas), 'Wrong singleton storage count/alignment')
    require(len(geps) == 2 and all(re.search(r'getelementptr inbounds \{ i32 \}, ptr %[^,]+, i32 0, i32 0$', l) for l in geps), 'Wrong nonpacked field-0 address calculation')
    calls = [l.strip() for l in lines if 'call ' in l]
    require(len(calls) == (3 if native else 2), 'Unexpected calls')
    require(any('call { i32 } @"singleton_roundtrip::make"(i32 42)' in l for l in calls), 'Missing direct aggregate result call')
    require(any(re.search(r'call i32 @"singleton_roundtrip::take"\(\{ i32 \} %[^)]+\)$', l) for l in calls), 'Missing direct aggregate parameter call')
    # Canonical make stores field 0 before its first whole-value load/return.
    make = text.split('define internal { i32 } @"singleton_roundtrip::make"', 1)[1].split('\n}', 1)[0]
    require(make.index('store i32 %arg.x, ptr %field.ptr.0, align 4') < make.index('load { i32 }') < make.index('ret { i32 }'), 'Constructor read before field initialization')
    require('store { i32 } %arg.p' in text and 'store { i32 } %t0' in text, 'Missing whole-value parameter/local stores')
    return dict(definitions=definitions, allocas=allocas, field_addresses=geps, calls=calls,
                abi='internal Cat aggregate Direct, NOT C ABI; no flattening, sret or byval')


def executable_format(path, windows):
    data = path.read_bytes()
    if windows:
        require(data[:2] == b'MZ', 'Not native PE')
        offset = int.from_bytes(data[60:64], 'little')
        require(data[offset:offset+6] == b'PE\0\0\x64\x86', 'Not AMD64 COFF/PE')
        require(int.from_bytes(data[offset+24:offset+26], 'little') == 0x20b, 'Not PE32+')
        return 'PE32+ AMD64 (native Windows)'
    require(data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 62, 'Not native ELF64 x86_64')
    return 'ELF64 x86_64 (native Linux)'


def main():
    parser = argparse.ArgumentParser()
    for name in ('production', 'layout-probe', 'cc', 'artifacts', 'profile', 'git'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--expected-sha', required=True)
    args = parser.parse_args()
    root = Path(args.artifacts).resolve(); root.mkdir(parents=True, exist_ok=True)
    result = dict(schema='cat-production-singleton-roundtrip/v1', status='RUNNING', profiles={}, stages={},
                  limitations=['No adapter aggregate codegen. No C FFI ABI claim.',
                               'Canonical generated LIR only; malformed-LIR, invalid slot/store/field/init acceptance remains a separate CLOSED gate.',
                               'Windows target sanitizer is not enabled by this probe.'])
    def save():
        (root/'evidence.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf8')
    try:
        windows = sys.platform == 'win32'
        require(sys.platform in ('linux', 'win32'), 'Native Linux or Windows required')
        require(args.profile == ('windows-x86_64-ucrt-coff' if windows else 'linux-x86_64-elf'), 'Profile mismatch; cross execution is not evidence')
        result.update(platform=platform.platform(), profile=args.profile, source_sha256=digest(SOURCE),
                      compiler_sha256=digest(args.production), layout_probe_sha256=digest(args.layout_probe))
        result['revision'] = good(run([args.git, '-C', REPO, 'rev-parse', 'HEAD']))
        require(result['revision']['stdout'].strip() == args.expected_sha, 'Not exact configured source SHA')
        require(not os.getenv('EXPECTED_HEAD_SHA') or args.expected_sha == os.environ['EXPECTED_HEAD_SHA'], 'Not exact requested CI head SHA')
        result['tools'] = {'version': good(run([args.cc, '--version'])), 'target': good(run([args.cc, '-dumpmachine']))}
        compiler = Path(args.production).resolve()
        (root/SOURCE.name).write_bytes(SOURCE.read_bytes())
        for stage in ('check', 'oir', 'lir', 'llvm'):
            record = good(run([compiler, stage, SOURCE])); result['stages'][stage] = record; save()
            require(not record['stderr_hex'], f'{stage}: unexpected diagnostic')
            (root/(stage+'.txt')).write_bytes(bytes.fromhex(record['stdout_hex']))
        inspect_stages(result['stages']['oir']['stdout'], result['stages']['lir']['stdout'])
        stage_ir = result['stages']['llvm']['stdout'].replace('\r\n', '\n')
        result['stage_ir'] = inspect_ir(stage_ir, native=False)
        for level, flags in [('O0', []), ('O2', ['--release'])]:
            directory = root/level; directory.mkdir(exist_ok=True)
            exe = directory/'production.exe'; ll = exe.with_suffix('.ll')
            # Remove only artifacts owned by this probe, never reuse stale success.
            for path in (exe, ll):
                if path.exists(): path.unlink()
            item = {}; result['profiles'][level] = item
            item['build'] = good(run([compiler, 'build', *flags, SOURCE, exe])); save()
            require(exe.is_file() and ll.is_file(), 'Missing freshly built production artifacts')
            item['native'] = run([exe])
            require(item['native']['exit'] == 42 and not item['native']['stdout_hex'] and not item['native']['stderr_hex'], f'Expected exit42 and empty stdout/stderr: {item["native"]}')
            item['executable_format'] = executable_format(exe, windows)
            text = ll.read_text(encoding='utf8'); item['ir'] = inspect_ir(text)
            wrapper = 'define i32 @main() {\nentry:\n  %entry.result.0 = call i32 @"singleton_roundtrip::main"()\n  ret i32 %entry.result.0\n}\n'
            require(text.endswith(wrapper), 'Missing exact native entry wrapper')
            require(text[:-len(wrapper)].rstrip() == stage_ir.rstrip(), 'Build IR differs from llvm stage beyond the verified native wrapper')
            triple = re.search(r'^target triple = "([^"]+)"$', text, re.M)
            require(triple and triple[1] == ('x86_64-w64-windows-gnu' if windows else 'x86_64-unknown-linux-gnu'), 'Wrong native production triple')
            effective = directory/'clang-effective.ll'
            item['effective_build'] = good(run([args.cc, '-Wno-override-module', '-S', '-emit-llvm', '-O0', '-x', 'ir', ll, '-o', effective]))
            effective_text = effective.read_text(encoding='utf8')
            dl = re.search(r'^target datalayout = "([^"]+)"$', effective_text, re.M)
            require(dl, 'No effective target DataLayout; do not infer from host C struct')
            item['layout_command'] = good(run([args.layout_probe, SOURCE, triple[1], dl[1]]))
            item['layout'] = json.loads(item['layout_command']['stdout'])
            item['raw_data_layout_origin'] = 'Cat production IR does not specify DataLayout; actual native Clang effective DataLayout inspected independently against SDK TargetMachine'
            item['sha256'] = {p.name: digest(p) for p in (exe, ll, effective)}
            save()
        result['status'] = 'PASS'; save()
        print(f'PASS exact production singleton round-trip {args.profile}: check/OIR/LIR/LLVM; Cat Direct parameter/result; target DataLayout; O0/O2 native exit42, empty stdout/stderr. Malformed-LIR gate remains CLOSED.')
        return 0
    except Exception as exc:
        result.update(status='FAIL', error=str(exc)); save()
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
