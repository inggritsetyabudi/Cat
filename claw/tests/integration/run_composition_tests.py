#!/usr/bin/env python3
"""Durable v0.1-v0.3 check/LLVM/build/run composition gate.

The driver emits an executable entry wrapper and links at -O2. Its generated
LLVM is also independently compiled at -O0 and -O2, with and without target
ASan/UBSan/LSan. Instrumented compiler coverage is a separate full-suite CI job.
"""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
FIX = HERE / "fixtures"
ART = HERE / "artifacts"
fallback = ROOT / "build" / "claw"
if not fallback.is_file():
    fallback = ROOT / "build-ucrt64-clang" / "claw.exe"
CLAW = Path(os.environ.get("CLAW_EXE", str(fallback))).resolve()
CASES = [
    ("composition_core", FIX / "composition_core.cat", "9\n11\n15\n12\n15\n8\nA\n2\n"),
    ("composition_workspace", FIX / "composition_workspace", "9\n5\n"),
    ("composition_implicit_result", FIX / "composition_implicit_result.cat", "42\nmissing\n5\nnone\n9\n"),
    ("composition_ownership_ranges", FIX / "composition_ownership_ranges.cat",
     "left\n10\nright\n20\nmissing\né\nboundary\nAéB\n0\n1\n1\n2\n254\n0\n255\n1\n1\n3\n"),
]
SAN_FLAGS = ["-fsanitize=address,undefined,leak", "-fno-omit-frame-pointer", "-g"]
SAN_ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

def run(cmd: list[str], *, env=None, input=None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, encoding="utf-8", capture_output=True,
                          check=False, timeout=60, env=env, input=input)

def fail(msg: str, proc: subprocess.CompletedProcess[str] | None = None) -> None:
    if proc is not None:
        msg += f"\nexit={proc.returncode}\nstdout={proc.stdout}\nstderr={proc.stderr}"
    raise RuntimeError(msg)

def execute(exe: Path, expected: str, *, sanitized=False) -> None:
    p = run([str(exe)], env=SAN_ENV if sanitized else None)
    if p.returncode != 0 or p.stdout != expected or (sanitized and p.stderr):
        fail(f"execution/output mismatch for {exe.name}; expected {expected!r}", p)

def main() -> int:
    if not CLAW.is_file() or not os.access(CLAW, os.X_OK):
        print(f"missing compiler executable: {CLAW}", file=sys.stderr)
        return 2
    clang = shutil.which("clang")
    if clang is None:
        fail("clang is required to verify and relink generated LLVM")
    ART.mkdir(parents=True, exist_ok=True)
    probe = ART / "sanitizer_probe.exe"
    probe.unlink(missing_ok=True)
    p = run([clang, *SAN_FLAGS, "-x", "c", "-", "-o", str(probe)],
            input="int main(void) { return 0; }\n")
    sanitized = p.returncode == 0
    if sanitized:
        p = run([str(probe)], env=SAN_ENV)
        sanitized = p.returncode == 0 and not p.stderr
    if not sanitized:
        if sys.platform.startswith("linux"):
            fail("Linux ASan/UBSan/LSan target runtime is mandatory", p)
        print(f"[composition/sanitizer/skip] target runtime unavailable on {sys.platform}: {p.stderr.strip()}")
    runs = 0
    for name, source, expected in CASES:
        print(f"[composition/check] {name}", flush=True)
        p = run([str(CLAW), "check", str(source)])
        if p.returncode:
            fail(f"check failed for {name}", p)
        ll = ART / f"{name}.ll"
        exe = ART / f"{name}.exe"
        ll.unlink(missing_ok=True)
        exe.unlink(missing_ok=True)
        p = run([str(CLAW), "llvm", str(source)])
        if p.returncode or not p.stdout or "::main" not in p.stdout:
            fail(f"LLVM emission failed for {name}", p)
        (ART / f"{name}.module.ll").write_text(p.stdout, encoding="utf-8")
        p = run([str(CLAW), "build", str(source), str(exe)])
        if p.returncode or not exe.is_file() or not ll.is_file():
            fail(f"native build or generated LLVM missing for {name}", p)
        if "define i32 @main()" not in ll.read_text(encoding="utf-8"):
            fail(f"native LLVM entry wrapper missing for {name}")
        execute(exe, expected)
        runs += 1
        for optimization in ("O0", "O2"):
            for instrumented in ([False, True] if sanitized else [False]):
                variant = ART / f"{name}.{optimization}{'.san' if instrumented else ''}.exe"
                variant.unlink(missing_ok=True)
                p = run([clang, "-Wno-override-module", f"-{optimization}",
                         *(SAN_FLAGS if instrumented else []), str(ll),
                         str(ROOT / "runtime" / "native_runtime.c"), "-o", str(variant)])
                if p.returncode or not variant.is_file():
                    fail(f"LLVM verification/relink failed for {variant.name}", p)
                execute(variant, expected, sanitized=instrumented)
                runs += 1
                print(f"[composition/target/pass] {variant.name}: exact stdout", flush=True)
        print(f"[composition/pass] {name}: check/LLVM/native + O0/O2 targets", flush=True)
    for companion in (HERE / "run_negative_composition_tests.py", HERE / "run_pattern_oracle.py"):
        if not companion.is_file():
            fail(f"required companion runner missing: {companion}")
        print(f"[composition/companion] {companion.name}", flush=True)
        p = subprocess.run([sys.executable, str(companion), str(CLAW)], check=False)
        if p.returncode:
            return p.returncode
    print(f"Composition gate passed: {len(CASES)} positive fixtures, {runs} exact-output target executions; target sanitizers={'PASS' if sanitized else 'SKIP'}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
