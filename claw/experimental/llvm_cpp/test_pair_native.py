#!/usr/bin/env python3
"""Native Tuple[Int32,Int32] adapter proof and fail-closed driver flag test."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from lld.test_lld import HarnessError, file_record, fresh, isolate, link_argv, profile, verify_exe, sha256
from test_poc import isolated_windows_emitter, verify_target
from test_singleton_native import production_case, stamp
from test_singleton_production import executable_format

FIXTURE = HERE.parents[1] / "tests/native/fixtures/revise_pair_i32_roundtrip.cat"
NO_ARITH = "fn make(x: Int32, y: Int32) -> (Int32, Int32) { (x, y) }\nfn take(p: (Int32, Int32)) -> Int32 { p.0 }\nfn main() -> Int32 { val p=make(42, 2) return take(p) }\n"
DESTRUCTURE = "fn make(x: Int32, y: Int32) -> (Int32, Int32) { (x, y) }\nfn take(p: (Int32, Int32)) -> Int32 { val (x, y) = p return x }\nfn main() -> Int32 { val p=make(42, 2) return take(p) }\n"
INVALID = {
    "mixed": "fn main() -> Int32 { val p = (42, true) return p.0 }\n",
    "nested": "fn main() -> Int32 { val p = ((42,), 2) return p.1 }\n",
    "triple": "fn main() -> Int32 { val p = (40, 1, 1) return p.0 }\n",
    "owned": 'fn main() -> Int32 { val s = String.from("x"); val p = (42, s); return p.0 }\n',
    "branch": "fn main() -> Int32 { val p = if true { (42, 0) } else { (41, 1) } return p.0 }\n",
}
MODES = {"default": (), "arithmetic": ("--arithmetic-i32",), "legacy": ("--runtime-i32", "--arithmetic-i32", "--strings")}


def run(argv, env=None, expected=None):
    p = subprocess.run([str(x) for x in argv], capture_output=True, env=env, timeout=180)
    row = {"argv": [str(x) for x in argv], "exit": p.returncode, "stdout_hex": p.stdout.hex(),
           "stderr_hex": p.stderr.hex(), "stdout": p.stdout.decode("utf-8", "replace"),
           "stderr": p.stderr.decode("utf-8", "replace")}
    if expected is not None and p.returncode != expected:
        raise HarnessError(f"exit mismatch: {row}")
    return row


def unsupported(row, label):
    text = row["stderr"]
    if row["exit"] != 1 or row["stdout_hex"] or not text or re.search(r"traceback|assertionerror|segmentation|internal compiler error|crash", text, re.I):
        raise HarnessError(f"{label}: bad rejection {row}")
    if not re.search(r"pair|tuple|unsupported|requires|flag|error", text, re.I):
        raise HarnessError(f"{label}: unsupported diagnostic missing {row}")


def env_for(args, root):
    root.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        if not args.readobj:
            raise HarnessError("Windows requires --readobj")
        iso, env = isolated_windows_emitter(root, args.poc, args.readobj)
        args.poc = iso / args.poc.name
        return env
    env = os.environ.copy()
    env["PATH"] = ""
    env["CC"] = env["CXX"] = env["CLAW_NATIVE_CLANG"] = "/__missing_compiler__"
    return env


def inspect_pair_ir(path):
    text = path.read_text(encoding="utf-8")
    if re.search(r"\b(?:undef|poison|sret|byval|inalloca|preallocated)\b", text):
        raise HarnessError(f"pair IR has invalid ABI/coercion marker: {path}")
    defs = re.findall(r"^define .*", text, re.M)
    make = next((x for x in defs if '::make"' in x), "")
    take = next((x for x in defs if '::take"' in x), "")
    if not re.search(r'define internal \{ i32, i32 \} @"[^" ]+::make"\(i32 %[^,]+, i32 %[^)]+\)', make):
        raise HarnessError(f"pair constructor is not Direct nonpacked aggregate ABI: {make}")
    if not re.search(r'define internal i32 @"[^" ]+::take"\(\{ i32, i32 \} %[^)]+\)', take):
        raise HarnessError(f"pair parameter is not Direct nonpacked aggregate ABI: {take}")
    allocas = [line.strip() for line in text.splitlines() if "alloca { i32, i32 }" in line]
    if not allocas or not all(line.endswith(", align 4") for line in allocas):
        raise HarnessError("pair aggregate storage is not 4-byte aligned")
    geps = [line.strip() for line in text.splitlines() if "getelementptr" in line and "{ i32, i32 }" in line]
    if not any(re.search(r", i32 0, i32 0$", line) for line in geps) or not any(re.search(r", i32 0, i32 1$", line) for line in geps):
        raise HarnessError("pair projections do not use nonpacked field-0/field-1 addresses")
    if not re.search(r"call \{ i32, i32 \} @\"[^\"]+::make\"\(i32 40, i32 2\)", text) and not re.search(r"call \{ i32, i32 \} @\"[^\"]+::make\"\(i32 42, i32 2\)", text):
        raise HarnessError("missing direct aggregate constructor call")
    if not re.search(r"call i32 @\"[^\"]+::take\"\(\{ i32, i32 \} %", text):
        raise HarnessError("missing direct aggregate parameter call")
    return {"definitions": defs, "allocas": allocas, "projections": geps,
            "abi": "Cat Direct aggregate pair; nonpacked i32 fields; no C ABI coercion"}


def main(args):
    for path in (args.poc, args.production, args.linker, args.runtime, args.manifest):
        if not path.is_file():
            raise HarnessError(f"required tool/package file missing: {path}")
    if not FIXTURE.is_file():
        raise HarnessError(f"fixture missing: {FIXTURE}")
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}:
        raise HarnessError("native x86_64 Linux/Windows only")
    expected = "windows-x86_64-ucrt-coff" if sys.platform == "win32" else "linux-x86_64-elf"
    if args.profile != expected:
        raise HarnessError(f"exact profile required: {expected}")
    args.claw = args.production.resolve()
    args.poc = args.poc.resolve()
    args.runtime = args.runtime.resolve()
    args.manifest = args.manifest.resolve()
    root = args.artifacts.resolve(); root.mkdir(parents=True, exist_ok=True)
    args._emitter_env = env_for(args, root / "emitter-isolation")
    inspector = args.readobj if sys.platform == "win32" else Path(shutil.which("readelf") or "/missing-readelf")
    args._linker, args._linker_env, args._linker_isolation = isolate(args.linker.resolve(), "external", root / "linker-isolation", inspector)
    prof = profile(args)
    version = run([args._linker, "-flavor", "gnu", "--version"], args._linker_env, 0)
    if not re.search(r"(?m)^LLD 23\.1\.3(?:\s|\()", version["stdout"]):
        raise HarnessError("external linker is not exact LLD 23.1.3")
    package_before = {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)}
    source = FIXTURE.read_bytes()
    production_sources = {"fixture": source.decode(), "no-arithmetic": NO_ARITH, "destructure": DESTRUCTURE}
    report = {"schema": "cat-pair-native/v2", "status": "RUNNING", "platform": sys.platform,
              "profile": args.profile, "production": [], "modes": {}, "rejections": [],
              "package_before": package_before}

    # Production is the reference path: check/OIR/LIR/LLVM and fresh O0/O2 native builds.
    for name, src in production_sources.items():
        production_case(args, root, name, src, report["production"])
        if {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)} != package_before:
            raise HarnessError(f"runtime/manifest changed during production case {name}")

    def emit(src, case, flags, enabled=True):
        d = root / case; d.mkdir(parents=True, exist_ok=True)
        s, o, ll = d / "probe.cat", d / "program.o", d / "program.ll"
        s.write_bytes(src.encode() if isinstance(src, str) else src); fresh(o); fresh(ll)
        argv = [args.poc, s, "-o", o, "--emit-ir", ll] + (["--pair-i32"] if enabled else []) + list(flags)
        result = run(argv, args._emitter_env)
        if result["exit"] != 0 or not o.is_file() or not ll.is_file():
            raise HarnessError(f"pair emit failed: {result}")
        target = verify_target(o, ll); abi = inspect_pair_ir(ll)
        return d, s, o, ll, result, target, abi

    def link_and_run(d, o):
        exe = d / ("program.exe" if sys.platform == "win32" else "program"); fresh(exe); mapfile = d / "link.map"; fresh(mapfile)
        argv = link_argv(args._linker, o, exe, prof, mapfile); pos = argv.index("--start-group"); argv[pos:pos] = [args.runtime, "--gc-sections"]
        if any(str(x).lower().endswith((".cat", ".ll", ".cpp")) for x in argv):
            raise HarnessError("source/IR passed to external LLD")
        run(argv, args._linker_env, 0); verify_exe(exe)
        native = run([exe], args._linker_env, 42)
        if native["stdout_hex"] or native["stderr_hex"]:
            raise HarnessError(f"nonempty native output {native}")
        return exe, native

    for name, src in production_sources.items():
        # Destructuring is deliberately a separate whole-value transfer proof.
        flags = ("--arithmetic-i32",) if name == "fixture" else ()
        for level in ("O0", "O2"):
            d, s, o, ll, e, target, abi = emit(src, f"adapter/{name}/{level}", flags)
            exe, native = link_and_run(d, o)
            report["modes"][f"{name}/{level}"] = {"emit": e, "target": target, "abi": abi,
                "object": file_record(o), "ir": file_record(ll), "exe": file_record(exe), "native": native}
            if {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)} != package_before:
                raise HarnessError("runtime/manifest changed during adapter proof")
    if FIXTURE.read_bytes() != source:
        raise HarnessError("fixture changed")

    def reject(src, case, flags=(), enabled=False):
        d = root / "rejections" / case; d.mkdir(parents=True, exist_ok=True)
        s, o, ll = d / "probe.cat", d / "program.o", d / "program.ll"; s.write_text(src)
        fresh(o); fresh(ll)
        argv = [args.poc, s, "-o", o, "--emit-ir", ll] + (["--pair-i32"] if enabled else []) + list(flags)
        first = run(argv, args._emitter_env); unsupported(first, case + "/absent")
        if o.exists() or ll.exists(): raise HarnessError(f"{case}: rejection published artifact")
        so, si = b"PAIR OBJECT SENTINEL\n", b"PAIR IR SENTINEL\n"; o.write_bytes(so); ll.write_bytes(si)
        second = run(argv, args._emitter_env); unsupported(second, case + "/sentinel")
        if o.read_bytes() != so or ll.read_bytes() != si: raise HarnessError(f"{case}: rejection clobbered sentinel")
        if list(d.rglob("*.tmp")): raise HarnessError(f"{case}: rejection temporary cleanup failed")
        report["rejections"].append({"name": case, "result": second})

    # Pair admission is exact: switch required, arithmetic does not imply pair, singleton is not an alias.
    for mode, flags in MODES.items():
        reject(source.decode(), f"fixture/{mode}/without-pair", flags)
        reject(NO_ARITH, f"no-arithmetic/{mode}/without-pair", flags)
        reject(source.decode(), f"fixture/{mode}/singleton", ("--singleton-i32", *flags))
    reject(source.decode(), "fixture/pair-only-addition", (), True)
    reject(source.decode(), "fixture/pair-plus-singleton", ("--singleton-i32",), True)
    reject(source.decode(), "fixture/pair-plus-runtime", ("--runtime-i32",), True)
    for name, src in INVALID.items():
        for mode, flags in MODES.items():
            reject(src, f"{name}/{mode}/without-pair", flags)
            reject(src, f"{name}/{mode}/singleton", ("--singleton-i32", *flags))
        reject(src, f"{name}/pair-enabled", (), True)
        reject(src, f"{name}/pair-enabled-arithmetic", ("--arithmetic-i32",), True)

    if {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)} != package_before:
        raise HarnessError("runtime/manifest changed by pair-native test")
    report["status"] = "PASS"
    (root / "evidence.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS pair native: production reference, Direct pair ABI, destructuring, O0/O2, external LLD 23.1.3, strict rejection sentinels")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--poc", "--emitter", dest="poc", type=Path, required=True)
    ap.add_argument("--production", "--claw", dest="production", type=Path, required=True)
    ap.add_argument("--linker", "--external", dest="linker", type=Path, required=True)
    ap.add_argument("--runtime", type=Path, required=True); ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--profile", required=True); ap.add_argument("--artifacts", type=Path, required=True)
    ap.add_argument("--ucrt-prefix", type=Path); ap.add_argument("--sysroot", type=Path); ap.add_argument("--readobj", type=Path)
    main(ap.parse_args())
