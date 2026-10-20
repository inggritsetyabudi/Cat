#!/usr/bin/env python3
"""End-to-end Tuple[Int32] singleton native proof."""
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
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from lld.test_lld import HarnessError, file_record, fresh, isolate, link_argv, profile, verify_exe, sha256
from test_poc import isolated_windows_emitter, verify_target
from test_singleton_production import executable_format

MODES = {
    "default": (),
    "local-binding-keys": ("--local-binding-keys",),
    "all-features-legacy-keys": ("--runtime-i32", "--arithmetic-i32", "--strings"),
    "all-features-local-keys": ("--runtime-i32", "--arithmetic-i32", "--strings", "--local-binding-keys"),
}

# All files deliberately have the same basename (probe.cat).  The paired sources
# therefore exercise byte identity, rather than a name-normalized approximation.
POSITIVES = {
    "canonical": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { p.0 }
fn main() -> Int32 { val p=make(42) return take(p) }
""",
    "projection": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { val x = p.0 return x }
fn main() -> Int32 { val p = make(42) return take(p) }
""",
    "destructure": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { val (x,) = p return x }
fn main() -> Int32 { val p = make(42) return take(p) }
""",
    "unused": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { return 42 }
fn main() -> Int32 { val p = make(42) return take(p) }
""",
    "wildcard": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { val (_,)=p return 42 }
fn main() -> Int32 { val p = make(42) return take(p) }
""",
    "shadow": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { val x = p.0 return x }
fn main() -> Int32 { val p = make(42) scope inner { val p = make(7) val x = p.0 } return take(p) }
""",
    "nested-scope": """fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { val x = p.0 return x }
fn main() -> Int32 { val p = make(42) scope outer_scope { scope inner_scope { val p = make(7) val x = p.0 } } return take(p) }
""",
}

NEGATIVES = {
    "oob-field": "fn main() -> Int32 { val p = (42,) return p.1 }\n",
    "wrong-arity": "fn main() -> Int32 { val (x, y) = (42,) return 42 }\n",
    "wrong-type": "fn main() -> Int32 { val p = (true,) val x: Int32 = p.0 return 42 }\n",
    "use-after-destructure": "fn main() -> Int32 { val p = (42,) val (x,) = p return p.0 }\n",
    "pair": "fn main() -> Int32 { val p = (1, 2) return p.0 }\n",
    "nested": "fn main() -> Int32 { val p = ((1,),) return p.0.0 }\n",
}
SOURCE_REJECTS = {"oob-field", "wrong-arity", "wrong-type", "use-after-destructure"}


def command(argv, env=None, expected=None):
    p = subprocess.run([str(x) for x in argv], capture_output=True, env=env, timeout=180)
    row = {"argv": [str(x) for x in argv], "exit": p.returncode,
           "stdout_hex": p.stdout.hex(), "stderr_hex": p.stderr.hex(),
           "stdout": p.stdout.decode("utf-8", "replace"),
           "stderr": p.stderr.decode("utf-8", "replace")}
    if expected is not None and p.returncode != expected:
        raise HarnessError(f"command exit {p.returncode}, expected {expected}: {row}")
    return row


def save_bytes(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def stamp(path):
    path = Path(path)
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "mtime_ns": path.stat().st_mtime_ns, "sha256": sha256(path)}


def assert_unsupported(row, label):
    if row["exit"] != 1 or row["stdout_hex"]:
        raise HarnessError(f"{label}: expected exact exit 1 and empty stdout: {row}")
    text = row["stderr"]
    if not text or re.search(r"traceback|assertionerror|segmentation|internal compiler error|crash", text, re.I):
        raise HarnessError(f"{label}: unsupported input did not produce a supported diagnostic: {row}")
    if not re.search(r"singleton|tuple|unsupported|requires|flag|error\[(semantic|ownership)\]", text, re.I):
        raise HarnessError(f"{label}: diagnostic is not a supported rejection: {row}")


def native_run(exe, env=None):
    row = command([exe], env=env)
    row["artifact"] = file_record(exe)
    return row


def emit_env(args, root):
    if sys.platform == "win32":
        if not args.readobj:
            raise HarnessError("Windows requires --readobj for the isolated emitter import closure")
        if not hasattr(args, "_emitter_env"):
            # The existing helper creates its child, not this caller-owned root.
            root.mkdir(parents=True, exist_ok=True)
            isolation, args._emitter_env = isolated_windows_emitter(root, args.poc, args.readobj)
            args.poc = isolation / args.poc.name
            if not args.poc.is_file():
                raise HarnessError("isolated Windows emitter is not a file: " + str(args.poc))
        return args._emitter_env
    env = os.environ.copy(); env["PATH"] = ""
    for key in ("CC", "CXX", "CLAW_NATIVE_CLANG"):
        env[key] = "/__cat_missing_compiler__"
    return env


def production_case(args, root, name, source, report):
    case = root / "production" / name; case.mkdir(parents=True, exist_ok=True)
    src = case / "probe.cat"; src.write_text(source)
    rows = {"name": name, "source": file_record(src), "stages": {}, "profiles": {}}
    report.append(rows)
    for stage in ("check", "oir", "lir", "llvm"):
        row = command([args.claw, stage, src], expected=0)
        if row["stderr_hex"]: raise HarnessError(f"production {name}/{stage} wrote diagnostics: {row}")
        out = case / f"{stage}.txt"; save_bytes(out, bytes.fromhex(row["stdout_hex"]))
        row["artifact"] = file_record(out); rows["stages"][stage] = row
    lir = Path(rows["stages"]["lir"]["artifact"]["path"]).read_bytes()
    rows["lir_sha256"] = hashlib.sha256(lir).hexdigest()
    for level, flags in (("O0", ()), ("O2", ("--release",))):
        d = case / level; d.mkdir(exist_ok=True)
        # Production Cat appends .exe when the requested output has no suffix,
        # including on Linux. Use the established explicit artifact convention.
        exe = d / "program.exe"; ll = exe.with_suffix(".ll")
        for p in (exe, ll): fresh(p)
        build = command([args.claw, "build", *flags, src, exe], expected=0)
        if not exe.is_file() or not ll.is_file(): raise HarnessError(f"production {name}/{level}: missing build artifacts")
        native = native_run(exe)
        if native["exit"] != 42 or native["stdout_hex"] or native["stderr_hex"]:
            raise HarnessError(f"production {name}/{level}: expected exit42/empty output: {native}")
        rows["profiles"][level] = {"build": build, "native": native, "ll": file_record(ll),
                                    "executable": executable_format(exe, sys.platform == "win32")}
    return rows


def runtime_argv(linker, obj, exe, prof, mapfile, runtime):
    argv = link_argv(linker, obj, exe, prof, mapfile)
    index = argv.index("--start-group")
    argv[index:index] = [runtime, "--gc-sections"]
    if any(str(x).lower().endswith((".cat", ".ll", ".c", ".cpp")) for x in argv):
        raise HarnessError("source/IR passed to final linker")
    return argv


def adapter_case(args, root, name, source, report, package_before):
    base = root / "adapter" / name; base.mkdir(parents=True, exist_ok=True)
    src = base / "probe.cat"; src.write_text(source)
    rows = {"name": name, "source": file_record(src), "modes": {}, "runtime_package_before": package_before}
    report.append(rows)
    for mode, flags in MODES.items():
        d = base / mode; d.mkdir(parents=True, exist_ok=True)
        obj, ir, exe = d / "program.o", d / "program.ll", d / ("program.exe" if sys.platform == "win32" else "program")
        for p in (obj, ir, exe, d / "link.map"): fresh(p)
        emit = command([args.poc, src, "-o", obj, "--emit-ir", ir, "--singleton-i32", *flags], env=args._emitter_env, expected=0)
        target = verify_target(obj, ir); prof = profile(args)
        argv = runtime_argv(args._linker, obj, exe, prof, d / "link.map", args.runtime)
        link = command(argv, env=args._linker_env, expected=0)
        binary = verify_exe(exe); native = native_run(exe, args._linker_env)
        if native["exit"] != 42 or native["stdout_hex"] or native["stderr_hex"]: raise HarnessError(f"adapter {name}/{mode}: wrong native result: {native}")
        rows["modes"][mode] = {"emit": emit, "link": link, "link_argv": [str(x) for x in argv], "target": target,
                                "object": file_record(obj), "ir": file_record(ir), "executable": binary, "native": native}
        # Same legacy mode, but without the singleton switch: fail closed and do not publish.
        od = d / "without-singleton"; od.mkdir(exist_ok=True); oo, oi = od / "program.o", od / "program.ll"
        so, si = b"sentinel object\n", b"sentinel ir\n"; oo.write_bytes(so); oi.write_bytes(si)
        off = command([args.poc, src, "-o", oo, "--emit-ir", oi, *flags], env=args._emitter_env)
        assert_unsupported(off, f"positive {name}/{mode} without singleton")
        if oo.read_bytes() != so or oi.read_bytes() != si: raise HarnessError(f"positive {name}/{mode}: sentinel overwritten")
        rows["modes"][mode]["without_singleton"] = off
    rows["runtime_package_after"] = {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)}
    if rows["runtime_package_after"] != package_before: raise HarnessError(f"runtime package mutated during {name}")
    return rows


def adapter_negative(args, root, name, source, report):
    base = root / "negative" / name; base.mkdir(parents=True, exist_ok=True)
    src = base / "probe.cat"; src.write_text(source); row = {"name": name, "source": file_record(src), "modes": {}}
    report.append(row)
    for mode, flags in MODES.items():
        d = base / mode; d.mkdir(parents=True, exist_ok=True); mode_row = {}
        for label, extra in (("enabled", ("--singleton-i32",)), ("disabled", ())):
            obj, ir = d / f"{label}.o", d / f"{label}.ll"; so, si = b"sentinel object\n", b"sentinel ir\n"; obj.write_bytes(so); ir.write_bytes(si)
            result = command([args.poc, src, "-o", obj, "--emit-ir", ir, *extra, *flags], env=args._emitter_env)
            assert_unsupported(result, f"negative {name}/{mode}/{label}")
            if obj.read_bytes() != so or ir.read_bytes() != si: raise HarnessError(f"negative {name}/{mode}/{label}: artifact overwritten")
            mode_row[label] = result
        row["modes"][mode] = mode_row
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--poc", "--emitter", dest="poc", type=Path, required=True)
    parser.add_argument("--claw", "--production", dest="claw", type=Path, required=True)
    parser.add_argument("--linker", "--external", dest="linker", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True); parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--profile", required=True); parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--ucrt-prefix", type=Path); parser.add_argument("--sysroot", type=Path); parser.add_argument("--readobj", type=Path)
    args = parser.parse_args()
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}: raise HarnessError("native x86_64 Linux/Windows only")
    expected_profile = "windows-x86_64-ucrt-coff" if sys.platform == "win32" else "linux-x86_64-elf"
    if args.profile != expected_profile: raise HarnessError(f"exact profile required: {expected_profile}")
    if not args.poc.is_file() or not args.claw.is_file() or not args.linker.is_file(): raise HarnessError("tools must name existing files")
    root = args.artifacts.resolve(); root.mkdir(parents=True, exist_ok=True)
    args._emitter_env = emit_env(args, root / "emitter-isolation")
    inspector = args.readobj if sys.platform == "win32" else Path(shutil.which("readelf") or "/missing-readelf")
    args._linker, args._linker_env, args._linker_isolation = isolate(args.linker.resolve(), "external", root / "linker-isolation", inspector)
    if not args.manifest.is_file() or not args.runtime.is_file(): raise HarnessError("runtime and manifest must exist")
    package_before = {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)}
    report = {"schema": "cat-singleton-native/v2", "status": "RUNNING", "platform": sys.platform, "profile": args.profile,
              "modes": {k: list(v) for k, v in MODES.items()}, "commands": [], "production": [], "adapter": [], "rejections": [], "package_before": package_before}
    evidence = root / "evidence.json"
    def save(): evidence.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    try:
        version = command([args._linker, "-flavor", "gnu", "--version"], args._linker_env, expected=0)
        if not re.search(r"(?m)^LLD 23\.1\.3(?:\s|\()", version["stdout"]): raise HarnessError("external linker is not exact LLD 23.1.3")
        report["lld_version"] = version; save()
        lir_bytes = {}
        for name, source in POSITIVES.items():
            item = production_case(args, root, name, source, report["production"]); lir_bytes[name] = Path(item["stages"]["lir"]["artifact"]["path"]).read_bytes()
            adapter_case(args, root, name, source, report["adapter"], package_before); save()
        if lir_bytes["projection"] != lir_bytes["destructure"]: raise HarnessError("projection/destructure LIR is not byte-identical")
        if lir_bytes["unused"] != lir_bytes["wildcard"]: raise HarnessError("unused-binding/wildcard LIR is not byte-identical")
        report["byte_identity"] = {"projection_destructure": True, "unused_wildcard": True}
        for name, source in NEGATIVES.items():
            pcase = root / "production-negative" / name; pcase.mkdir(parents=True, exist_ok=True); src = pcase / "probe.cat"; src.write_text(source)
            check = command([args.claw, "check", src]);
            if name in SOURCE_REJECTS:
                if check["exit"] != 1 or check["stdout_hex"] or not re.search(r"error\[(semantic|ownership)\]", check["stderr"]) or "error[parse]" in check["stderr"] or re.search(r"traceback|internal compiler error|crash", check["stderr"], re.I): raise HarnessError(f"source negative {name} lacked expected diagnostic: {check}")
            elif check["exit"] != 0 or check["stdout"].replace("\r\n", "\n") != "Check passed for module: probe\n" or check["stderr"]: raise HarnessError(f"production negative {name} did not PASS cleanly: {check}")
            report["rejections"].append({"name": name, "production_check": check}); adapter_negative(args, root, name, source, report["rejections"]); save()
        if package_before != {"runtime": stamp(args.runtime), "manifest": stamp(args.manifest)}: raise HarnessError("runtime package changed")
        report["status"] = "PASS"; save(); print(json.dumps({"status": "PASS", "production": len(POSITIVES), "adapter_modes": len(MODES), "rejections": len(NEGATIVES), "evidence": str(evidence)})); return 0
    except Exception as exc:
        report.update(status="FAIL", error=str(exc)); save(); print(str(exc), file=sys.stderr); return 1


if __name__ == "__main__":
    raise SystemExit(main())
