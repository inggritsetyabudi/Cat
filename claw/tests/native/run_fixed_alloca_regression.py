#!/usr/bin/env python3
"""Fixed-size loop storage: actual LLVM/build/native O0/O2 regression gates."""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEST_DIR = Path(__file__).resolve().parent
FIXTURES = TEST_DIR / "fixtures"
ARTIFACTS = TEST_DIR / "artifacts" / "fixed-alloca"
WINDOWS = os.name == "nt" or sys.platform.startswith(("msys", "cygwin"))
EXPECTED_INT16 = """1
1
1
0
0
-2
-1
0
1
2
5
-1
0
0
1
1
2
empty-exclusive
empty-reversed
equal-inclusive
equal-item
start-once
end-once
1
-32768
32767
65536
-32768
0
0
32768
32767
65535
65536
"""
RECORDS: list[dict] = []


def native_arg(path: Path | str) -> str:
    value = str(path)
    if WINDOWS and shutil.which("cygpath"):
        return subprocess.check_output(["cygpath", "-w", value], text=True).strip()
    return value


def compiler_path() -> str:
    if configured := os.environ.get("CLAW_EXE"):
        return native_arg(configured)
    for candidate in (ROOT / "build/claw", ROOT / "build-ucrt64-clang/claw.exe", ROOT / "build-ucrt64-clang/claw-codex.exe"):
        if candidate.is_file():
            return native_arg(candidate)
    raise RuntimeError("missing compiler executable; set CLAW_EXE")


def run(args: list[str], *, limit_stack: bool = False, env=None) -> subprocess.CompletedProcess[str]:
    preexec = None
    stack_bytes = None
    if limit_stack and sys.platform.startswith("linux"):
        import resource
        soft, _ = resource.getrlimit(resource.RLIMIT_STACK)
        stack_bytes = 1024 * 1024 if soft == resource.RLIM_INFINITY else min(soft, 1024 * 1024)
        def child_limit() -> None:
            _, hard = resource.getrlimit(resource.RLIMIT_STACK)
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            resource.setrlimit(resource.RLIMIT_STACK, (stack_bytes, hard))
        preexec = child_limit
    result = subprocess.run(args, text=True, capture_output=True, env=env, preexec_fn=preexec, timeout=90)
    RECORDS.append({"argv": args, "stack_bytes": stack_bytes, "returncode": result.returncode,
                    "stdout": result.stdout, "stderr": result.stderr})
    (ARTIFACTS / "commands.json").write_text(json.dumps(RECORDS, indent=2) + "\n")
    return result


def require_success(args: list[str], label: str) -> subprocess.CompletedProcess[str]:
    result = run(args)
    if result.returncode:
        raise AssertionError(f"{label}: exit={result.returncode}, stdout={result.stdout!r}, stderr={result.stderr!r}")
    return result


def assert_allocas_in_entry(ll: str, label: str) -> int:
    function = None
    block = entry = None
    seen = 0
    bad: list[str] = []
    for line_no, line in enumerate(ll.splitlines(), 1):
        if line.startswith("define "):
            function = line
            block = entry = None
        elif line == "}":
            function = None
        elif function is not None:
            if match := re.match(r"^([A-Za-z0-9_.$-]+):(?:\s*;.*)?$", line):
                block = match.group(1)
                if entry is None:
                    entry = block
            if re.search(r"\balloca\b", line):
                seen += 1
                if entry is None or block != entry:
                    bad.append(f"{label}:{line_no}:{block or '<no block>'}:{line.strip()}")
    if bad or not seen:
        raise AssertionError("alloca outside first block or missing storage: " + ", ".join(bad[:8]))
    return seen


def assert_native(exe: Path, expected: str, label: str, *, sanitized: bool = False) -> None:
    env = None
    if sanitized:
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    result = run([native_arg(exe)], limit_stack=not sanitized, env=env)
    if result.returncode or result.stdout.replace("\r\n", "\n") != expected or result.stderr:
        raise AssertionError(f"{label}: exit={result.returncode}, stdout={result.stdout!r}, stderr={result.stderr!r}")


def main() -> int:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    compiler = compiler_path()
    clang = shutil.which("clang") or shutil.which("clang.exe")
    if not clang:
        raise RuntimeError("clang is required for actual LLVM/native regression gates")
    cases = [("revise_int16_range", EXPECTED_INT16),
             ("revise_fixed_alloca_stress", "625023\n"),
             ("revise_fixed_alloca_condition", "200000\n200006\n")]
    alloca_checks = native_checks = sanitizer_checks = compile_commands = 0
    for name, expected in cases:
        fixture = FIXTURES / f"{name}.cat"
        check = require_success([compiler, "check", native_arg(fixture)], name + "/check")
        if not check.stdout.startswith("Check passed for module: "):
            raise AssertionError(f"{name}: missing successful check result: {check.stdout!r}")
        llvm = require_success([compiler, "llvm", native_arg(fixture)], name + "/llvm")
        direct = ARTIFACTS / f"{name}_direct.ll"
        direct.write_text(llvm.stdout)
        alloca_checks += assert_allocas_in_entry(llvm.stdout, name + "/claw llvm")
        require_success([clang, "-O0", "-c", native_arg(direct), "-o", native_arg(direct.with_suffix(".o"))], name + "/LLVM verify")
        built = ARTIFACTS / f"{name}_built.exe"
        require_success([compiler, "build", native_arg(fixture), native_arg(built)], name + "/build")
        generated = built.with_suffix(".ll")
        if not generated.is_file():
            raise AssertionError(f"{name}: build did not preserve native LLVM")
        alloca_checks += assert_allocas_in_entry(generated.read_text(), name + "/native LLVM")
        compile_commands += 4
        # Explicit relinks avoid assumptions about the driver's default profile.
        for opt in ("-O0", "-O2"):
            exe = ARTIFACTS / f"{name}_{opt[1:]}.exe"
            args = [clang, opt, "-std=c11", native_arg(generated), native_arg(ROOT / "runtime/native_runtime.c")]
            require_success([*args, "-o", native_arg(exe)], name + "/link " + opt)
            compile_commands += 1
            assert_native(exe, expected, name + "/" + opt)
            native_checks += 1
            if sys.platform.startswith("linux"):
                instrumented = exe.with_name(exe.stem + "_san.exe")
                require_success([*args, "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-o", native_arg(instrumented)], name + "/san link " + opt)
                compile_commands += 1
                assert_native(instrumented, expected, name + "/san " + opt, sanitized=True)
                sanitizer_checks += 1
        print(f"[fixed-alloca/pass] {name}: check, llvm, build, native O0/O2 exact stdout/exit", flush=True)
    if sys.platform.startswith("linux"):
        print("LINUX_STACK_LIMIT maximum_bytes=1048576 (native sanitizers use unchanged host stack)")
    elif WINDOWS:
        print("WINDOWS_STACK native linker defaults; raw status and PE headers tested by probe_windows_stack.py")
    else:
        print(f"STACK_LIMIT unsupported platform={sys.platform}; native execution and IR gates still required")
    print(f"fixed-alloca regression: cases={len(cases)} compiler_link_commands={compile_commands} native_assertions={native_checks} native_sanitizer_assertions={sanitizer_checks} alloca_checks={alloca_checks}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, subprocess.SubprocessError, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
