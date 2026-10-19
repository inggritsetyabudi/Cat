#!/usr/bin/env python3
"""LLD worker/executable proof; neither mode invokes a compiler driver.

Original 41-case emission/cc-comparator tests remain unchanged. This additional
stage consumes their exact objects, then links and runs them in compiler-free
linker environments. Inspection tools run separately, never as link inputs.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from test_poc import verify_target, imported_dlls


class HarnessError(RuntimeError):
    pass


def command(argv, env, expected=None):
    result = subprocess.run([str(x) for x in argv], capture_output=True, text=True,
                            env=env, timeout=45)
    if expected is not None and result.returncode != expected:
        raise HarnessError(f"exit {result.returncode}, expected {expected}: {argv}\n{result.stdout}\n{result.stderr}")
    return result


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        while block := source.read(1 << 20):
            digest.update(block)
    return digest.hexdigest()


def file_record(path):
    path = Path(path)
    return {'path': str(path.absolute()), 'realpath': str(path.resolve()),
            'bytes': path.stat().st_size, 'sha256': sha256(path)}


def fresh(path):
    if path.is_dir():
        raise HarnessError(f"output is a directory: {path}")
    path.unlink(missing_ok=True)


def tool(path):
    # Keep ld.lld's alias spelling: resolving it to lld changes argv[0] dispatch.
    result = Path(path).expanduser().absolute()
    if not result.is_file():
        raise HarnessError(f"missing explicit tool: {result}")
    return result


def existing(paths, label):
    for path in paths:
        if path.is_file():
            return path.absolute()
    raise HarnessError(f"missing {label}: {[str(p) for p in paths]}")


def profile(args):
    if sys.platform == 'linux':
        if args.sysroot and args.sysroot.resolve() != Path('/'):
            raise HarnessError('Only native Ubuntu24 x64 GCC13 profile is validated; no alternate sysroot/cross support')
        lib = Path('/usr/lib/x86_64-linux-gnu')
        gcc = Path('/usr/lib/gcc/x86_64-linux-gnu/13')
        loader = Path('/lib64/ld-linux-x86-64.so.2')
        startup = [lib/'crt1.o', lib/'crti.o', gcc/'crtbegin.o']
        ending = [gcc/'crtend.o', lib/'crtn.o']
        libraries = [lib/'libc.so', gcc/'libgcc.a', gcc/'libgcc_s.so']
        search = [gcc, lib]
        target = 'elf_x86_64'
        description = 'native Ubuntu24 x86_64, GCC13/glibc, non-PIE ET_EXEC'
    else:
        if not args.ucrt_prefix:
            raise HarnessError('Native Windows requires explicit --ucrt-prefix; no MSVC/MSVCRT fallback')
        prefix = args.ucrt_prefix.absolute()
        lib = prefix/'lib'
        versions = sorted(p for p in (lib/'gcc/x86_64-w64-mingw32').glob('*') if p.is_dir())
        if len(versions) != 1:
            raise HarnessError(f"require one explicit installed GCC runtime directory: {versions}")
        gcc = versions[0]
        startup = [existing([lib/'crt2.o'], 'UCRT crt2.o'),
                   existing([gcc/'crtbegin.o', lib/'crtbegin.o'], 'crtbegin.o')]
        ending = [existing([gcc/'crtend.o', lib/'crtend.o'], 'crtend.o')]
        libraries = []
        for name in ('mingw32', 'gcc', 'gcc_eh', 'moldname', 'mingwex', 'ucrt',
                     'kernel32', 'advapi32', 'shell32', 'user32'):
            choices = [gcc/f'lib{name}.a', lib/f'lib{name}.a']
            if name == 'gcc_eh' and not any(p.is_file() for p in choices):
                choices = [gcc/'libgcc_s.a', lib/'libgcc_s.a']
            libraries.append(existing(choices, name))
        loader, search, target = None, [lib, gcc], 'i386pep'
        description = 'native Windows x64 MinGW UCRT, GNU flavor, PE32+ console'
    required = startup + ending + libraries + ([loader] if loader else [])
    if any(not p.is_file() for p in required):
        raise HarnessError(f"missing runtime profile inputs: {[str(p) for p in required if not p.is_file()]}")
    return {'target': target, 'startup': startup, 'ending': ending,
            'libraries': libraries, 'search': search, 'loader': loader,
            'description': description, 'required_files': [file_record(p) for p in required]}


def link_argv(linker, obj, output, prof, mapfile):
    argv = [linker, '-flavor', 'gnu', '-m', prof['target'], '--trace', f'--Map={mapfile}']
    if prof['loader']:
        argv += ['-dynamic-linker', prof['loader']]
    else:
        argv += ['--subsystem', 'console']
    argv += ['-o', output]
    for directory in prof['search']:
        argv += ['-L', directory]
    return argv + prof['startup'] + [obj, '--start-group'] + prof['libraries'] + ['--end-group'] + prof['ending']


def verify_exe(path):
    data = path.read_bytes()
    if sys.platform == 'linux':
        if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (2, 62):
            raise HarnessError('not an ELF64 little-endian ET_EXEC x86_64 executable')
        return {'format': 'ELF64', 'type': 'ET_EXEC', 'machine': 62, **file_record(path)}
    if path.suffix.lower() != '.exe' or data[:2] != b'MZ':
        raise HarnessError('missing fresh native .exe/MZ')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x8664 or struct.unpack_from('<H', data, pe+24)[0] != 0x20b or struct.unpack_from('<H', data, pe+92)[0] != 3:
        raise HarnessError('not PE32+ AMD64 console')
    return {'format': 'PE32+', 'machine': 'AMD64 (0x8664)', 'subsystem': 'console (3)', **file_record(path)}


def isolate(linker, label, directory, readobj):
    env = os.environ.copy()
    if sys.platform == 'linux':
        env['PATH'] = ''
        return linker, env, {'PATH': '', 'application': file_record(linker)}
    if not readobj:
        raise HarnessError('Windows DLL audit requires explicit --readobj')
    directory.mkdir(parents=True, exist_ok=True)
    for previous in directory.iterdir():
        if not previous.is_file() or (previous.suffix.lower() != '.dll' and previous.name != linker.name):
            raise HarnessError(f"unexpected content in linker isolation: {previous}")
        previous.unlink()
    system = Path(os.environ.get('SystemRoot', 'C:/Windows'))/'System32'
    search = [linker.parent] + [Path(p) for p in os.environ.get('PATH', '').split(os.pathsep) if p]
    queue, seen, records = [linker], set(), []
    while queue:
        source = queue.pop(0)
        if source.name.lower() in seen:
            continue
        seen.add(source.name.lower())
        target = directory/source.name
        shutil.copy2(source, target)
        imports = imported_dlls(readobj, target)
        records.append({**file_record(target), 'original': str(source), 'imports': sorted(imports)})
        for name in imports:
            if Path(name).name != name:
                raise HarnessError('DLL import is not a basename')
            if name.lower().startswith(('llvm', 'libllvm')):
                if label == 'embedded' or not re.search(r'(?<!\d)23(?!\d)', name):
                    raise HarnessError(f"unexpected/mixed LLVM DLL in {label}: {name}")
            if name.lower().startswith(('api-ms-win', 'ext-ms-win')) or (system/name).is_file():
                continue
            dependency = existing([p/name for p in search], name)
            queue.append(dependency)
    if any(p.name != linker.name and p.suffix.lower() != '.dll' for p in directory.iterdir()):
        raise HarnessError('compiler or second executable in isolated linker directory')
    env['PATH'] = str(directory)
    return directory/linker.name, env, {'PATH': str(directory), 'files': records,
        'unique_file_bytes': sum(r['bytes'] for r in records)}


def inspect(exe, directory, inspector):
    if not inspector:
        raise HarnessError('missing native object-inspection tool')
    flags = [('headers', ['-h']), ('dynamic', ['-d']), ('symbols', ['-s']), ('comment', ['-p', '.comment'])] if sys.platform == 'linux' else [('headers', ['--file-headers']), ('dynamic', ['--coff-imports']), ('symbols', ['--symbols'])]
    report = {}
    for name, options in flags:
        argv = [inspector, *options, exe]
        result = command(argv, os.environ.copy(), expected=0)
        (directory/f'inspect.{name}').write_text(result.stdout + result.stderr)
        report[name] = {'argv': [str(x) for x in argv], 'text': result.stdout}
    if sys.platform == 'linux':
        if 'Linker: LLD 23.1.3' not in report['comment']['text']:
            raise HarnessError('ELF comment lacks actual exact LLD linker identity')
        needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', report['dynamic']['text'])
        if 'libc.so.6' not in needed:
            raise HarnessError('expected explicit glibc runtime dependency missing')
        report['runtime_dependencies'] = needed
    else:
        deps = sorted(imported_dlls(inspector, exe))
        if 'msvcrt.dll' in {x.lower() for x in deps}:
            raise HarnessError('UCRT profile unexpectedly imports MSVCRT directly')
        if not any(x.lower() == 'ucrtbase.dll' or x.lower().startswith('api-ms-win-crt') for x in deps):
            raise HarnessError('UCRT import evidence missing')
        report['runtime_dependencies'] = deps
    return report


def baseline_cases(root):
    entries = json.loads((root/'results.json').read_text())
    counts = Counter(x.get('kind') for x in entries if x.get('status') == 'pass')
    if len(entries) != 41 or counts != {'differential': 15, 'rejection': 16, 'io': 9, 'isolation': 1}:
        raise HarnessError(f"original 41-case PASS contract not satisfied: {counts}")
    return [x for x in entries if x['kind'] == 'differential']


def run_mode(label, linker, args, base, output, prof, cases):
    root = output/label
    root.mkdir(parents=True, exist_ok=True)
    inspector = tool(args.readobj) if sys.platform == 'win32' else tool(shutil.which('readelf') or '/missing-readelf')
    active, env, isolation = isolate(linker, label, root/'linker-isolation', inspector)
    for name in ('clang', 'clang++', 'cc', 'gcc', 'cl'):
        if shutil.which(name, path=env['PATH']) is not None:
            raise HarnessError(f"compiler accessible in isolated link PATH: {name}")
    version = command([active, '--version'], env, expected=0)
    if not re.search(r'(?m)^LLD 23\.1\.3(?:\s|\()', version.stdout):
        raise HarnessError('linker is not exact LLD23.1.3')
    records = []
    suffix = '.exe' if sys.platform == 'win32' else ''
    for entry in cases:
        name = entry['name']
        obj = base/name/'program.o'
        ir = base/name/'program.ll'
        target = verify_target(obj, ir)
        before = sha256(obj)
        directory = root/name
        directory.mkdir(exist_ok=True)
        exe, trace, mapfile = directory/('program'+suffix), directory/'link.trace', directory/'link.map'
        for path in (exe, trace, mapfile):
            fresh(path)
        argv = link_argv(active, obj, exe, prof, mapfile)
        linked = command(argv, env)
        trace.write_text(linked.stdout)
        (directory/'link.stderr').write_text(linked.stderr)
        if linked.returncode or not mapfile.is_file() or not mapfile.stat().st_size or not trace.stat().st_size:
            raise HarnessError(f"{label}/{name} link failed: {linked.stderr}")
        binary = verify_exe(exe)
        native = command([exe], env)
        expected = entry['run']
        old = command([base/name/('program'+suffix)], os.environ.copy())
        actual = {'exit': native.returncode, 'stdout': native.stdout, 'stderr': native.stderr}
        if actual != expected or native.returncode != entry['exit'] or actual != {'exit': old.returncode, 'stdout': old.stdout, 'stderr': old.stderr}:
            raise HarnessError(f"{label}/{name} native result differs from original41-case oracle")
        if sha256(obj) != before:
            raise HarnessError('link modified original object')
        audit = inspect(exe, directory, inspector)
        records.append({'name': name, 'status': 'pass', 'object': file_record(obj),
            'object_target': target, 'link_argv': [str(x) for x in argv], 'link_exit': linked.returncode,
            'trace': str(trace), 'map': str(mapfile), 'executable': binary, 'run': actual,
            'audit': audit})
    literal = base/'space ; literal'/'object ; x.o'
    directory = root/'space ; literal'
    directory.mkdir(exist_ok=True)
    exe = directory/('program ; x'+suffix)
    fresh(exe)
    literal_argv = link_argv(active, literal, exe, prof, directory/'link.map')
    linked = command(literal_argv, env, expected=0)
    (directory/'link.trace').write_text(linked.stdout)
    verify_exe(exe)
    native = command([exe], env, expected=13)
    if native.stdout or native.stderr:
        raise HarnessError('literal-path output changed')
    negatives = []
    for name in ('malformed', 'missinglib', 'missingstartup'):
        exe = root/(f'negative-{name}'+suffix)
        fresh(exe)
        obj = literal
        if name == 'malformed':
            obj = root/'bad.o'
            obj.write_bytes(b'not an object\n')
        argv = link_argv(active, obj, exe, prof, root/f'{name}.map')
        if name == 'missinglib':
            argv[argv.index(prof['libraries'][0])] = root/'nonexistent.a'
        elif name == 'missingstartup':
            argv[argv.index(prof['startup'][0])] = root/'nonexistent-crt.o'
        failed = command(argv, env)
        if not failed.returncode or exe.exists():
            raise HarnessError(f"{label}/{name} unexpectedly succeeded or left executable")
        negatives.append({'name': name, 'argv': [str(x) for x in argv], 'exit': failed.returncode,
                          'stdout': failed.stdout, 'stderr': failed.stderr})
    timings = []
    for iteration in range(10):
        exe = root/(f'warm-{iteration}'+suffix)
        fresh(exe)
        argv = link_argv(active, base/'constant'/'program.o', exe, prof, root/f'warm-{iteration}.map')
        start = time.perf_counter_ns()
        linked = command(argv, env, expected=0)
        elapsed = (time.perf_counter_ns()-start)/1e6
        verify_exe(exe)
        native = command([exe], env, expected=42)
        if native.stdout or native.stderr:
            raise HarnessError('warm output mismatch')
        timings.append({'argv': [str(x) for x in argv], 'milliseconds': elapsed, 'exit': native.returncode})
    print(f'[pass/lld-{label}] 15 identical objects, literal path, 3 failures, 10 verified warm links', flush=True)
    return {'mode': label, 'version': version.stdout.strip(), 'application': file_record(linker),
            'isolation': isolation, 'profile': prof['description'], 'runtime_profile_inputs': prof['required_files'],
            'cases': records, 'literal': {'argv': [str(x) for x in literal_argv], 'exit': 13},
            'negative_cases': negatives, 'warm_links': timings,
            'warm_median_ms': statistics.median(x['milliseconds'] for x in timings)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embedded', type=Path)
    parser.add_argument('--external', type=Path)
    parser.add_argument('--baseline-artifacts', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--readobj', type=Path)
    parser.add_argument('--sysroot', type=Path)
    parser.add_argument('--ucrt-prefix', type=Path)
    args = parser.parse_args()
    if not args.embedded and not args.external:
        parser.error('supply an explicit linker; no compiler fallback exists')
    if platform.machine().lower() not in {'x86_64', 'amd64'} or sys.platform not in {'linux', 'win32'}:
        raise HarnessError('native Linux x86_64 or native Windows x64 only')
    base, output = args.baseline_artifacts.absolute(), args.artifacts.absolute()
    output.mkdir(parents=True, exist_ok=True)
    cases, prof = baseline_cases(base), profile(args)
    report = {'platform': sys.platform, 'baseline_pass_counts': dict(Counter(x['kind'] for x in json.loads((base/'results.json').read_text()))),
              'note': 'old41 object emission/cc comparison is separate; only this link stage has compiler-free PATH', 'modes': []}
    for label, path in (('embedded', args.embedded), ('external', args.external)):
        if path:
            report['modes'].append(run_mode(label, tool(path), args, base, output, prof, cases))
    (output/'testdata.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': 'PASS', 'modes': [x['mode'] for x in report['modes']], 'artifacts': str(output)}))


if __name__ == '__main__':
    try:
        main()
    except (HarnessError, AssertionError, OSError, subprocess.SubprocessError, struct.error) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
