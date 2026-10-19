#!/usr/bin/env python3
"""Int32-only runtime package proof; existing 41-case fixtures/oracles unchanged."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lld.test_lld import (profile, link_argv, verify_exe, isolate, inspect,
                          command, HarnessError, file_record, fresh, sha256)
from test_poc import verify_target

CASES = {
    'zero': ('fn main() { println(0) }', '0\n', 0),
    'forty_two': ('fn main() -> Int32 { println(42) return 7 }', '42\n', 7),
    'max_helper': ('fn show(x: Int32) { println(x) }\nfn main() { show(2147483647) }', '2147483647\n', 0),
    'mutable': ('fn main() {\nvar x: Int32\nx = 5\nprintln(x)\nx = 42\nprintln(x)\n}', '5\n42\n', 0),
    'multiple': ('fn main() { println(42) println(42) println(0) }', '42\n42\n0\n', 0),
}


def stamp(path):
    path = Path(path)
    return (path.stat().st_size, path.stat().st_mtime_ns, sha256(path))


def run_bytes(argv, env, expected=None):
    started = time.perf_counter_ns()
    result = subprocess.run([str(x) for x in argv], capture_output=True, env=env, timeout=45)
    elapsed = (time.perf_counter_ns() - started) / 1e6
    if expected is not None and result.returncode != expected:
        raise HarnessError(f'exit {result.returncode}, expected {expected}: {argv}\n{result.stdout!r}\n{result.stderr!r}')
    return result, elapsed


def native_record(result):
    return {'exit': result.returncode, 'stdout_hex': result.stdout.hex(),
            'stderr_hex': result.stderr.hex(),
            'stdout': result.stdout.decode('utf-8').replace('\r\n', '\n'),
            'stderr': result.stderr.decode('utf-8').replace('\r\n', '\n')}


def no_compiler(env, label):
    for name in ('clang', 'clang++', 'cc', 'gcc', 'gcc-13', 'g++', 'cl', 'clang-23'):
        if shutil.which(name, path=env['PATH']) is not None:
            raise HarnessError(f'{label}: compiler accessible: {name}')
    env['CC'] = env['CXX'] = env['CLAW_NATIVE_CLANG'] = '/__cat_missing_compiler__'
    env['NoDefaultCurrentDirectoryInExePath'] = '1'


def runtime_argv(active, obj, exe, prof, directory, archive):
    argv = link_argv(active, obj, exe, prof, directory/'link.map')
    index = argv.index('--start-group')
    argv[index:index] = ([archive] if archive else []) + ['--gc-sections']
    if any(str(x).lower().endswith(('.cat', '.ll', '.c', '.cpp')) for x in argv):
        raise HarnessError('source/IR passed to final linker')
    return argv


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('emitter', 'production', 'runtime', 'external', 'artifacts', 'manifest'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--profile', required=True)
    parser.add_argument('--readobj', type=Path)
    parser.add_argument('--ucrt-prefix', type=Path)
    parser.add_argument('--sysroot', type=Path)
    args = parser.parse_args()
    if platform.machine().lower() not in {'x86_64', 'amd64'} or sys.platform not in {'linux', 'win32'}:
        raise HarnessError('native Linux/Windows x64 only')
    root = args.artifacts.absolute()
    root.mkdir(parents=True, exist_ok=True)
    args.runtime = args.runtime.absolute()
    manifest = json.loads(args.manifest.read_text())
    source = Path(manifest['source']['path'])
    runtime_object = Path(manifest['object']['path'])
    if Path(manifest['archive']['path']).resolve() != args.runtime.resolve():
        raise HarnessError('manifest is for a different runtime archive')
    for role, path in (('source', source), ('object', runtime_object), ('archive', args.runtime)):
        if sha256(path) != manifest[role]['sha256']:
            raise HarnessError(f'runtime package hash mismatch: {role}')
    protected = {str(p): stamp(p) for p in (source, runtime_object, args.runtime, args.manifest)}
    prof = profile(args)
    inspector = args.readobj if sys.platform == 'win32' else Path(shutil.which('readelf') or '/missing-readelf')
    normal = os.environ.copy()
    report = {'platform': sys.platform, 'profile': args.profile, 'package': manifest,
              'note': 'Preparation/oracles/audit use normal tools; per-source emit/link/run use isolated environments. CRT/OS are not hermetic.',
              'cases': [], 'rejections': [], 'negative_links': [], 'warm_runs': []}
    prepared = []
    suffix = '.exe' if sys.platform == 'win32' else ''
    # Prepare EVERY reference before the compiler-free build loop; never compile runtime C there.
    for name, (source_text, expected, exit_code) in CASES.items():
        directory = root/name
        directory.mkdir(exist_ok=True)
        cat = directory/(name+'.cat')
        cat.write_text(source_text+'\n')
        reference = directory/'reference.exe'
        fresh(reference)
        build_argv = [args.production, 'build', cat, reference]
        built, build_ms = run_bytes(build_argv, normal, 0)
        reference_run, _ = run_bytes([reference], normal, exit_code)
        expected_bytes = expected.replace('\n', '\r\n').encode() if sys.platform == 'win32' else expected.encode()
        if reference_run.stdout != expected_bytes or reference_run.stderr:
            raise HarnessError(f'{name}: production does not satisfy independent output oracle')
        prepared.append((name, directory, cat, native_record(reference_run), build_argv, build_ms))
    active, link_env, link_iso = isolate(args.external.absolute(), 'external', root/'linker-isolation', inspector)
    emitter, emit_env, emit_iso = isolate(args.emitter.absolute(), 'emitter', root/'emitter-isolation', inspector)
    no_compiler(link_env, 'link')
    no_compiler(emit_env, 'emit')
    version = command([active, '--version'], link_env, 0)
    if not re.search(r'^LLD 23\.1\.3(?:\s|\()', version.stdout):
        raise HarnessError('not exact LLD23.1.3')
    report.update({'link_isolation': link_iso, 'emit_isolation': emit_iso,
                   'lld_version': version.stdout.strip(), 'crt_inputs': prof['required_files']})
    for name, directory, cat, expected, build_argv, build_ms in prepared:
        obj, ir, exe = directory/'program.o', directory/'program.ll', directory/('program'+suffix)
        for path in (obj, ir, exe, directory/'link.map'):
            fresh(path)
        emit_argv = [emitter, cat, '-o', obj, '--emit-ir', ir, '--runtime-i32']
        emitted, emit_ms = run_bytes(emit_argv, emit_env, 0)
        target = verify_target(obj, ir)
        ir_text = ir.read_text()
        if not re.search(r'declare void @"?claw\.runtime\.println\.i32"?\(i32\)', ir_text):
            raise HarnessError('missing exact void(i32) runtime ABI')
        if re.search(r'claw\.runtime\.(?:string|anchor|arena)\.|@(?:malloc|free)\b', ir_text):
            raise HarnessError('owned/heap runtime referenced by emitted IR')
        before_object = stamp(obj)
        argv = runtime_argv(active, obj, exe, prof, directory, args.runtime)
        linked, link_ms = run_bytes(argv, link_env, 0)
        (directory/'link.trace').write_bytes(linked.stdout+linked.stderr)
        binary = verify_exe(exe)
        native, run_ms = run_bytes([exe], link_env, expected['exit'])
        actual = native_record(native)
        if actual != expected:
            raise HarnessError(f'{name}: native runtime differs from production/oracle: {actual}')
        audit = inspect(exe, directory, inspector)
        symbol_argv = [inspector, '--symbols', exe] if sys.platform == 'win32' else [inspector, '--wide', '--syms', exe]
        symbols = command(symbol_argv, normal, 0).stdout
        (directory/'full-symbols.txt').write_text(symbols)
        if 'claw.runtime.println.i32' not in symbols:
            raise HarnessError('final symbol table lacks reachable Int32 runtime')
        if re.search(r'claw\.runtime\.(?:string|anchor|arena)\.', symbols):
            raise HarnessError('unreferenced owned runtime was not GC-discarded')
        object_symbols = command([inspector, '--symbols', obj] if sys.platform == 'win32' else [inspector, '--wide', '--syms', obj], normal, 0).stdout
        (directory/'object-symbols.txt').write_text(object_symbols)
        if 'claw.runtime.println.i32' not in object_symbols:
            raise HarnessError('runtime reference absent from program object')
        if stamp(obj) != before_object:
            raise HarnessError('link modified input object')
        report['cases'].append({'name': name, 'status': 'pass', 'emit_argv': list(map(str, emit_argv)),
            'link_argv': list(map(str, argv)), 'object': file_record(obj), 'target': target,
            'executable': binary, 'run': actual, 'production_run': expected,
            'production_build_argv': list(map(str, build_argv)), 'production_build_ms': build_ms,
            'emit_ms': emit_ms, 'link_ms': link_ms, 'run_ms': run_ms, 'audit': audit})
    # Unsupported source is rejected even with the new option, before publishing artifacts.
    for name, content, flag in [
        ('default-runtime', 'fn main() { println(42) }', False),
        ('borrowed-text', 'fn main() { println("text") }', True),
        ('owned-string', 'fn main() { val s = String.from("text") println(s) }', True),
        ('bool', 'fn main() { println(true) }', True),
        ('print', 'fn main() { print(42) }', True),
    ]:
        directory = root/('reject-'+name)
        directory.mkdir(exist_ok=True)
        cat, obj, ir = directory/'input.cat', directory/'program.o', directory/'program.ll'
        cat.write_text(content+'\n')
        for path in (obj, ir):
            fresh(path)
        argv = [emitter, cat, '-o', obj, '--emit-ir', ir] + (['--runtime-i32'] if flag else [])
        failed, _ = run_bytes(argv, emit_env)
        if failed.returncode == 0 or obj.exists() or ir.exists() or b'unsupported' not in failed.stderr:
            raise HarnessError(f'{name}: unsupported runtime/owned path not rejected fail-closed: {failed.stderr!r}')
        report['rejections'].append({'name': name, 'status': 'pass', 'argv': list(map(str, argv)), 'exit': failed.returncode, 'stderr': failed.stderr.decode()})
    valid_obj = root/'zero/program.o'
    bad_obj = root/'unavailable-symbol.o'
    content = valid_obj.read_bytes()
    if b'claw.runtime.println.i32' not in content:
        raise HarnessError('cannot construct unavailable-symbol fixture')
    bad_obj.write_bytes(content.replace(b'claw.runtime.println.i32', b'claw.runtime.println.bad'))
    bad_archive = root/'malformed-runtime.a'
    bad_archive.write_bytes(b'not an archive\n')
    for name, obj, archive, diagnostic in [
        ('missing-runtime-symbol', valid_obj, None, 'claw.runtime.println.i32'),
        ('unavailable-symbol', bad_obj, args.runtime, 'claw.runtime.println.bad'),
        ('malformed-archive', valid_obj, bad_archive, 'malformed-runtime.a'),
        ('missing-archive-file', valid_obj, root/'nonexistent-runtime.a', 'nonexistent-runtime.a'),
    ]:
        directory = root/('negative-'+name)
        directory.mkdir(exist_ok=True)
        exe = directory/('program'+suffix)
        fresh(exe)
        argv = runtime_argv(active, obj, exe, prof, directory, archive)
        failed, _ = run_bytes(argv, link_env)
        if failed.returncode == 0 or exe.exists() or diagnostic not in (failed.stdout+failed.stderr).decode('utf-8', 'replace'):
            raise HarnessError(f'{name}: missing exact link failure or stale executable')
        report['negative_links'].append({'name': name, 'status': 'pass', 'argv': list(map(str, argv)),
            'exit': failed.returncode, 'stdout': failed.stdout.decode('utf-8', 'replace'), 'stderr': failed.stderr.decode('utf-8', 'replace')})
    # Tiny warm diagnostics, not a controlled whole-compiler speed comparison.
    directory = root/'warm'
    directory.mkdir(exist_ok=True)
    for index in range(10):
        obj, ir, exe = directory/'program.o', directory/'program.ll', directory/('program'+suffix)
        for path in (obj, ir, exe):
            fresh(path)
        _, emit_ms = run_bytes([emitter, root/'zero/zero.cat', '-o', obj, '--emit-ir', ir, '--runtime-i32'], emit_env, 0)
        argv = runtime_argv(active, obj, exe, prof, directory, args.runtime)
        _, link_ms = run_bytes(argv, link_env, 0)
        verify_exe(exe)
        native, run_ms = run_bytes([exe], link_env, 0)
        if native.stdout != (b'0\r\n' if sys.platform == 'win32' else b'0\n') or native.stderr:
            raise HarnessError('warm runtime output changed')
        report['warm_runs'].append({'iteration': index, 'emit_ms': emit_ms, 'link_ms': link_ms, 'run_ms': run_ms})
    if any(stamp(Path(path)) != before for path, before in protected.items()):
        raise HarnessError('per-source path modified/recompiled runtime source/object/archive/manifest')
    report['immutable_inputs'] = {path: {'bytes': before[0], 'mtime_ns': before[1], 'sha256': before[2], 'unchanged': True} for path, before in protected.items()}
    report['warm_medians_ms'] = {key: statistics.median(row[key] for row in report['warm_runs']) for key in ('emit_ms', 'link_ms', 'run_ms')}
    (root/'testdata.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': 'PASS', 'cases': len(report['cases']), 'rejections': len(report['rejections']), 'negative_links': len(report['negative_links']), 'warm_medians_ms': report['warm_medians_ms']}))


if __name__ == '__main__':
    try:
        main()
    except (HarnessError, AssertionError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print('FAIL: '+str(error), file=sys.stderr)
        raise SystemExit(1)
