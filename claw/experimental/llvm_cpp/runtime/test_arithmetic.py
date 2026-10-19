#!/usr/bin/env python3
"""E2E signed-Int32 arithmetic proof for the compiler-free runtime link path.

This intentionally is a sibling of test_runtime.py: the original 41-case proof is
not changed.  Production observations are made first in both compiler profiles;
only afterwards are emitter objects linked with isolated LLD and the immutable
runtime archive.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

if sys.platform == "linux":
    import resource

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lld.test_lld import (profile, link_argv, verify_exe, isolate, inspect,
                          command, HarnessError, file_record, fresh, sha256)
from test_poc import verify_target

FATAL = "fatal: integer division or remainder by zero; divisor must be nonzero\n"
MIN = "(-2147483647 - 1)"
# Parameterized operations make the signed arithmetic and guards observable in IR.
CASES = {
    "add_normal": ("a + b", "20", "22", "42"),
    "add_overflow": ("a + b", "2147483647", "1", "-2147483648"),
    "sub_normal": ("a - b", "20", "22", "-2"),
    "sub_overflow": ("a - b", MIN, "1", "2147483647"),
    "mul_normal": ("a * b", "-7", "3", "-21"),
    "mul_overflow": ("a * b", "1073741824", "2", "-2147483648"),
    "neg_normal": ("-a", "7", "0", "-7"),
    "neg_min": ("-a", MIN, "0", "-2147483648"),
    "div_negative": ("a / b", "-7", "3", "-2"),
    "rem_negative": ("a % b", "-7", "3", "-1"),
    "div_two_negative": ("a / b", "-7", "-3", "2"),
    "rem_two_negative": ("a % b", "-7", "-3", "-1"),
    "div_min_minus_one": ("a / b", MIN, "-1", "-2147483648"),
    "rem_min_minus_one": ("a % b", MIN, "-1", "0"),
    "div_zero_dynamic": ("a / b", "7", "0", None),
    "rem_zero_dynamic": ("a % b", "7", "0", None),
}
# Keep these independent of the production probe so the test catches accidental
# fixture/oracle coupling.
EXTRA = {
    "nested": ("fn main() { println((20 + 5) * (3 - 1)) }", "50\n", 0),
    "composed_functions": ("fn twice(x: Int32) -> Int32 { return x * 2 }\nfn main() { println(twice(7) + 1) }", "15\n", 0),
    "multiple_divisions": ("fn main() { println(20 / 3) println(20 % 3) println(-20 / 3) }", "6\n2\n-6\n", 0),
    "if_arithmetic": ("fn main() { val x: Int32 = 3; if x < 4 { println(x + 9) } else { println(0) } }", "12\n", 0),
    "short_circuit_rhs": ("fn main() { if false and (7 / 0 > 0) { println(9) } println(42) }", "42\n", 0),
    "loop_arithmetic": ("fn main() { var x: Int32; x = 0; loop x < 3 { x = x + 1 }; println(x * 2) }", "6\n", 0),
    "div_zero_literal": ("fn main() { println(7 / 0) }", None, None),
    "rem_zero_literal": ("fn main() { println(7 % 0) }", None, None),
    "div_zero_constant": ("fn main() { println(7 / (3 - 3)) }", None, None),
    "dead_zero_branch": ("fn main() { if false { println(7 / 0) } println(42) }", "42\n", 0),
    "min_literal": ("fn main() { println(-2147483648) }", "-2147483648\n", 0),
    "literal_min_edges": ("fn main() { println(-2147483648 / -1) println(-2147483648 % -1) }", "-2147483648\n0\n", 0),
    "dynamic_div_composition": ("fn calculate(a: Int32, b: Int32) -> Int32 { return (a / b) + (a % b) + (a / (b + 1)) }\nfn main() { println(calculate(20, 3)) }", "13\n", 0),
}


def stamp(path):
    path = Path(path)
    return {"bytes": path.stat().st_size, "mtime_ns": path.stat().st_mtime_ns, "sha256": sha256(path)}


def run_bytes(argv, env, expected=None):
    started = time.perf_counter_ns()
    result = subprocess.run([str(x) for x in argv], capture_output=True, env=env, timeout=60)
    elapsed = (time.perf_counter_ns() - started) / 1e6
    if expected is not None and result.returncode != expected:
        raise HarnessError(f"exit {result.returncode}, expected {expected}: {argv}\n{result.stdout!r}\n{result.stderr!r}")
    return result, elapsed


def native_record(result):
    return {"exit": result.returncode, "stdout_hex": result.stdout.hex(),
            "stderr_hex": result.stderr.hex(),
            "stdout": result.stdout.decode("utf-8", "replace").replace("\r\n", "\n"),
            "stderr": result.stderr.decode("utf-8", "replace").replace("\r\n", "\n")}


def no_compiler(env, label):
    for name in ("clang", "clang++", "cc", "gcc", "gcc-13", "g++", "cl", "clang-23"):
        if shutil.which(name, path=env["PATH"]) is not None:
            raise HarnessError(f"{label}: compiler accessible: {name}")
    env["CC"] = env["CXX"] = env["CLAW_NATIVE_CLANG"] = "/__cat_missing_compiler__"
    env["NoDefaultCurrentDirectoryInExePath"] = "1"


def runtime_argv(active, obj, exe, prof, directory, archive):
    argv = link_argv(active, obj, exe, prof, directory / "link.map")
    index = argv.index("--start-group")
    argv[index:index] = ([archive] if archive else []) + ["--gc-sections"]
    if any(str(x).lower().endswith((".cat", ".ll", ".c", ".cpp")) for x in argv):
        raise HarnessError("source/IR passed to final linker")
    return argv


def production_source(name):
    if name in CASES:
        op, a, b, _ = CASES[name]
        return f"fn calculate(a: Int32, b: Int32) -> Int32 {{ return {op} }}\nfn main() -> Int32 {{ println(calculate({a}, {b})) return 0 }}\n"
    return EXTRA[name][0].replace(";", "\n") + "\n"


def expected_for(name):
    if name in CASES:
        result = CASES[name][3]
        return (None, None) if result is None else (result + "\n", 0)
    return EXTRA[name][1:]


def assert_native(result, expected):
    out, code = expected
    if out is None:
        if result.stdout:
            raise HarnessError(f"division defect emitted stdout: {result.stdout!r}")
        actual = result.stderr.decode("utf-8", "replace").replace("\r\n", "\n")
        if sys.platform == "linux" and actual != FATAL:
            raise HarnessError(f"non-canonical Linux defect diagnostic: {actual!r}")
        if FATAL not in actual:
            raise HarnessError(f"missing required defect diagnostic: {actual!r}")
        if result.returncode == 0:
            raise HarnessError("division defect returned success")
        if sys.platform == "linux" and result.returncode != -signal.SIGABRT:
            raise HarnessError(f"expected native SIGABRT, got {result.returncode}")
    else:
        native = native_record(result)
        if result.returncode != code or result.stdout != (out.replace("\n", "\r\n").encode() if sys.platform == "win32" else out.encode()) or result.stderr:
            raise HarnessError(f"output mismatch: expected {out!r}/{code}, got {native}")


def production_argv(compiler, release, source, exe):
    flags = ["--release"] if release else []
    return [compiler, "build", *flags, source, exe]


def audit_ir(text, name):
    # The checks are deliberately textual and independent from the adapter's
    # implementation: no poison-producing flags or unsigned arithmetic may leak.
    if re.search(r"\b(?:nsw|nuw|poison)\b", text):
        raise HarnessError(f"{name}: nsw/nuw/poison present")
    if re.search(r"\b(?:udiv|urem)\b", text):
        raise HarnessError(f"{name}: unsigned division/remainder present")
    if re.search(r"\b(?:sdiv|srem)\s+[^\n,]+,\s*(?:0|-1)\b", text):
        raise HarnessError(f"{name}: literal zero/-1 signed divisor")
    if "claw.runtime.defect.division_by_zero" not in text and name in {"div_zero_dynamic", "rem_zero_dynamic"}:
        raise HarnessError(f"{name}: missing observable defect hook")
    if name.startswith("div_") or name.startswith("rem_"):
        if not re.search(r"safe|zero|overflow|with\.overflow|icmp", text, re.I):
            raise HarnessError(f"{name}: no visible divisor/overflow guard")
        if "noreturn" not in text or "unreachable" not in text or not re.search(r'call void @"?claw\.runtime\.defect\.division_by_zero"?\(\)', text):
            raise HarnessError(f"{name}: missing noreturn defect call/unreachable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emitter", "production", "runtime", "external", "artifacts", "manifest"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--readobj", type=Path)
    parser.add_argument("--ucrt-prefix", type=Path)
    parser.add_argument("--sysroot", type=Path)
    args = parser.parse_args()
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}:
        raise HarnessError("native Linux/Windows x64 only")
    if sys.platform == "linux":
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    root = args.artifacts.absolute(); root.mkdir(parents=True, exist_ok=True)
    args.runtime = args.runtime.absolute()
    manifest = json.loads(args.manifest.read_text())
    source_pkg, object_pkg = Path(manifest["source"]["path"]), Path(manifest["object"]["path"])
    if Path(manifest["archive"]["path"]).resolve() != args.runtime.resolve():
        raise HarnessError("manifest is for a different runtime archive")
    for role, path in (("source", source_pkg), ("object", object_pkg), ("archive", args.runtime)):
        if sha256(path) != manifest[role]["sha256"]:
            raise HarnessError(f"runtime package hash mismatch: {role}")
    protected = {str(p): stamp(p) for p in (source_pkg, object_pkg, args.runtime, args.manifest)}
    prof = profile(args)
    inspector = args.readobj if sys.platform == "win32" else Path(shutil.which("readelf") or "/missing-readelf")
    normal = os.environ.copy(); suffix = ".exe" if sys.platform == "win32" else ""
    names = list(CASES) + list(EXTRA)
    report = {"platform": sys.platform, "profile": args.profile, "package": manifest,
              "cases": [], "rejections": [], "negative_links": [], "warm_runs": [],
              "note": "O0/O2 are production optimization parity references, not Cat debug-safety profiles. One fixed-codegen PoC object is compared against both; only the bounded native release/wrapping slice is tested."}
    prepared = []
    # Both production references are complete before either compiler-free loop.
    for name in names:
        directory = root / name; directory.mkdir(exist_ok=True)
        cat = directory / (name + ".cat"); cat.write_text(production_source(name))
        refs = {}
        for release, label in ((False, "O0"), (True, "release_O2")):
            # Production appends .exe when no extension is supplied, also on Linux.
            exe = directory / (label + ".exe"); fresh(exe)
            argv = production_argv(args.production, release, cat, exe)
            built, build_ms = run_bytes(argv, normal, 0)
            observed, run_ms = run_bytes([exe], normal)
            expected = expected_for(name); assert_native(observed, expected)
            refs[label] = {"build_argv": list(map(str, argv)), "build_ms": build_ms,
                           "build_output": native_record(built), "run_ms": run_ms, "run": native_record(observed)}
        # Compare exact bytes, including any Windows CRT abort diagnostic.
        if refs["O0"]["run"] != refs["release_O2"]["run"]:
            raise HarnessError(f"{name}: O0/release reference parity mismatch")
        for run in (refs["O0"]["run"], refs["release_O2"]["run"]):
            if expected_for(name)[0] is None and FATAL not in run["stderr"]:
                raise HarnessError(f"{name}: reference lost required defect diagnostic")
        prepared.append((name, directory, cat, refs))
    active, link_env, link_iso = isolate(args.external.absolute(), "external", root / "linker-isolation", inspector)
    emitter, emit_env, emit_iso = isolate(args.emitter.absolute(), "emitter", root / "emitter-isolation", inspector)
    no_compiler(link_env, "link"); no_compiler(emit_env, "emit")
    version = command([active, "--version"], link_env, 0)
    if not re.search(r"^LLD 23\.1\.3(?:\s|\()", version.stdout):
        raise HarnessError("not exact LLD23.1.3")
    report.update({"link_isolation": link_iso, "emit_isolation": emit_iso, "lld_version": version.stdout.strip(), "crt_inputs": prof["required_files"]})
    for name, directory, cat, refs in prepared:
        obj, ir = directory / "program.o", directory / "program.ll"
        fresh(obj); fresh(ir)
        emit_argv = [emitter, cat, "-o", obj, "--emit-ir", ir, "--arithmetic-i32", "--runtime-i32"]
        emitted, emit_ms = run_bytes(emit_argv, emit_env, 0)
        target = verify_target(obj, ir); ir_text = ir.read_text(); audit_ir(ir_text, name)
        if not re.search(r'declare void @"?claw\.runtime\.println\.i32"?\(i32\)', ir_text):
            raise HarnessError(f"{name}: missing exact println.i32 ABI")
        if re.search(r"claw\.runtime\.(?:string|anchor|arena)\.|@(malloc|free)\b", ir_text):
            raise HarnessError(f"{name}: owned runtime/heap referenced")
        row = {"name": name, "source": cat.read_text(), "source_path": str(cat),
               "production_references": refs, "object": file_record(obj), "ir": file_record(ir),
               "emit_argv": list(map(str, emit_argv)), "target": target, "emit_ms": emit_ms, "profiles": {}}
        for label in ("O0", "release_O2"):
            exe = directory / (label + ".linked" + suffix); fresh(exe)
            argv = runtime_argv(active, obj, exe, prof, directory / label, args.runtime)
            (directory / label).mkdir(exist_ok=True)
            linked, link_ms = run_bytes(argv, link_env, 0)
            (directory / (label + ".link.trace")).write_bytes(linked.stdout + linked.stderr)
            binary = verify_exe(exe); native, run_ms = run_bytes([exe], link_env)
            assert_native(native, expected_for(name))
            # Exact exit/stdout/stderr parity against each independently built reference.
            actual, reference = native_record(native), refs[label]["run"]
            if actual != reference:
                raise HarnessError(f"{name}/{label}: reference parity mismatch")
            audit = inspect(exe, directory / label, inspector)
            row["profiles"][label] = {"link_argv": list(map(str, argv)), "link_ms": link_ms, "run_ms": run_ms, "run": actual, "production": reference, "executable": binary, "audit": audit}
        symbols_argv = [inspector, "--symbols", directory / ("release_O2.linked" + suffix)] if sys.platform == "win32" else [inspector, "--wide", "--syms", directory / "release_O2.linked"]
        symbols = command(symbols_argv, normal, 0).stdout; (directory / "full-symbols.txt").write_text(symbols)
        if expected_for(name)[0] is not None and "claw.runtime.println.i32" not in symbols:
            raise HarnessError(f"{name}: reachable println symbol absent")
        if expected_for(name)[0] is None and "claw.runtime.defect.division_by_zero" not in symbols:
            raise HarnessError(f"{name}: required division defect symbol absent")
        report["cases"].append(row)
    # Default mode and unsupported types must fail closed and publish no object/IR.
    rejects = [("default-off", "fn main() { println(1 + 2) }", ["--runtime-i32"]),
               ("uint32", "fn calculate(x: UInt32) -> UInt32 { return x + 1_UInt32 }\nfn main() { println(0) }", ["--arithmetic-i32", "--runtime-i32"]),
               ("int64", "fn calculate(x: Int64) -> Int64 { return x + 1_Int64 }\nfn main() { println(0) }", ["--arithmetic-i32", "--runtime-i32"]),
               ("float", "fn calculate(x: Float64) -> Float64 { return x + 1.5 }\nfn main() { println(0) }", ["--arithmetic-i32", "--runtime-i32"]),
               ("bitwise", "fn main() { println(1 & 1) }", ["--arithmetic-i32", "--runtime-i32"])]
    for name, source, flags in rejects:
        d = root / ("reject-" + name); d.mkdir(exist_ok=True); cat, obj, ir = d / "input.cat", d / "program.o", d / "program.ll"; cat.write_text(source + "\n"); fresh(obj); fresh(ir)
        failed, _ = run_bytes([emitter, cat, "-o", obj, "--emit-ir", ir, *flags], emit_env)
        diagnostic = rb"unsupported|not supported|not yet"
        if name == "bitwise":
            # Bitwise is staged in the frontend; do not claim adapter coverage
            # from this parse rejection (the API test rejects its opcode too).
            diagnostic = rb"error\[parse\]: expected '\)' after arguments"
        if failed.returncode == 0 or obj.exists() or ir.exists() or not re.search(diagnostic, failed.stderr.lower()): raise HarnessError(f"{name}: did not reject without artifacts: {failed.stderr!r}")
        report["rejections"].append({"name": name, "exit": failed.returncode, "stderr": failed.stderr.decode("utf-8", "replace")})
    # Missing archive is a link-time missing runtime symbol proof, without compiling anything.
    d = root / "negative-missing-runtime"; d.mkdir(exist_ok=True); exe = d / ("program" + suffix); fresh(exe)
    failed, _ = run_bytes(runtime_argv(active, root / "div_negative" / "program.o", exe, prof, d, d / "missing-runtime.a"), link_env)
    if failed.returncode == 0 or exe.exists() or "missing-runtime.a" not in (failed.stdout + failed.stderr).decode("utf-8", "replace"): raise HarnessError("missing archive negative link did not fail cleanly")
    report["negative_links"].append({"name": "missing-runtime-archive", "exit": failed.returncode, "stdout": failed.stdout.decode("utf-8", "replace"), "stderr": failed.stderr.decode("utf-8", "replace")})
    fresh(exe)
    missing_symbol_argv = runtime_argv(active, root / "div_negative" / "program.o", exe, prof, d, None)
    failed, _ = run_bytes(missing_symbol_argv, link_env)
    if failed.returncode == 0 or exe.exists() or b"claw.runtime.defect.division_by_zero" not in failed.stdout + failed.stderr:
        raise HarnessError("missing division defect symbol did not fail cleanly")
    report["negative_links"].append({"name": "missing-division-defect-symbol", "argv": list(map(str, missing_symbol_argv)),
                                     "exit": failed.returncode, "stdout": failed.stdout.decode("utf-8", "replace"), "stderr": failed.stderr.decode("utf-8", "replace")})
    # Five warm runs use an existing object/archive only; no compiler/runtime build is allowed.
    d = root / "warm"; d.mkdir(exist_ok=True)
    for i in range(5):
        exe = d / ("program" + suffix); fresh(exe); started = time.perf_counter_ns(); run_bytes(runtime_argv(active, root / "add_normal" / "program.o", exe, prof, d, args.runtime), link_env, 0); verify_exe(exe); native, _ = run_bytes([exe], link_env); assert_native(native, expected_for("add_normal")); report["warm_runs"].append({"iteration": i, "elapsed_ms": (time.perf_counter_ns() - started) / 1e6})
    for path, before in protected.items():
        if stamp(path) != before: raise HarnessError(f"immutable runtime input changed: {path}")
    report["immutable_inputs"] = {p: {**v, "unchanged": True} for p, v in protected.items()}
    report["warm_median_ms"] = statistics.median(x["elapsed_ms"] for x in report["warm_runs"])
    (root / "testdata.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "PASS", "cases": len(report["cases"]), "rejections": len(report["rejections"]), "negative_links": len(report["negative_links"]), "profiles": 2}))


if __name__ == "__main__":
    try: main()
    except (HarnessError, AssertionError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr); raise SystemExit(1)
