#!/usr/bin/env python3
"""Windows baseline/fixed native status and unmodified PE-stack diagnostic."""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from run_fixed_alloca_regression import EXPECTED_INT16, WINDOWS, native_arg

OVERFLOW = 0xC00000FD


def command(args: list[str], records: list[dict]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, text=True, capture_output=True,
                            encoding="utf-8", errors="replace", timeout=180)
    records.append({"argv": args, "returncode": result.returncode,
                    "stdout": result.stdout, "stderr": result.stderr})
    if result.returncode:
        raise AssertionError(f"command failed: {args!r}: {result.returncode}: {result.stderr}")
    return result


def assert_windows_llvm(text: str, label: str) -> None:
    if 'target triple = "x86_64-w64-windows-gnu"' not in text:
        raise AssertionError(f"{label}: LLVM output is not the Windows native target")


def parse_pe(path: Path) -> dict[str, int | str]:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise AssertionError(f"{path}: missing MZ header")
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe:pe + 4] != b"PE\0\0":
        raise AssertionError(f"{path}: missing PE signature")
    optional = pe + 24
    magic = int.from_bytes(data[optional:optional + 2], "little")
    if magic not in (0x20B, 0x10B):
        raise AssertionError(f"{path}: unsupported optional-header magic {magic:#x}")
    width = 8 if magic == 0x20B else 4
    reserve = int.from_bytes(data[optional + 72:optional + 72 + width], "little")
    commit = int.from_bytes(data[optional + 72 + width:optional + 72 + 2 * width], "little")
    if reserve <= 0 or not (0 < commit <= reserve):
        raise AssertionError(f"{path}: invalid PE stack reserve/commit: {reserve}/{commit}")
    return {"format": "PE32+" if width == 8 else "PE32", "stack_reserve": reserve, "stack_commit": commit}


def run_probe(probe: Path, exe: Path, out: Path, err: Path, status: Path, records: list[dict]) -> dict:
    # Python owns POSIX paths under MSYS; native file arguments are converted for
    # the UCRT64 helper, which uses Win32 APIs directly without a shell.
    result = command([str(probe), native_arg(exe), native_arg(out), native_arg(err), native_arg(status)], records)
    raw = status.read_text(encoding="ascii").strip()
    match = re.fullmatch(r"raw_exit_code=0x([0-9A-Fa-f]{1,8}) decimal=(\d+)", raw)
    if not match or int(match.group(1), 16) != int(match.group(2)):
        raise AssertionError(f"invalid raw probe record {status}: {raw!r}")
    return {"raw_status_file": raw, "raw_exit_code": int(match.group(1), 16),
            "stdout": out.read_bytes().decode("utf-8", "replace"),
            "stderr": err.read_bytes().decode("utf-8", "replace"),
            "probe_stdout": result.stdout, "probe_stderr": result.stderr}


def one_variant(name: str, compiler: Path, runtime: Path, source: Path, probe: Path,
                root: Path, records: list[dict], failures: list[str]) -> None:
    work = root / name
    work.mkdir(parents=True, exist_ok=True)
    check = command([str(compiler), "check", native_arg(source)], records)
    if not check.stdout.startswith("Check passed for module: "):
        raise AssertionError(f"{name}: missing successful check result")
    direct = command([str(compiler), "llvm", native_arg(source)], records)
    direct_path = work / f"{name}-direct.ll"
    direct_path.write_text(direct.stdout, encoding="utf-8")
    assert_windows_llvm(direct.stdout, f"{name}/llvm")
    clang = shutil.which("clang") or shutil.which("clang.exe")
    if not clang:
        raise RuntimeError("clang is required")
    command([clang, "-O0", "-c", native_arg(direct_path), "-o", native_arg(direct_path.with_suffix(".o"))], records)
    built = work / f"{name}-built.exe"
    command([str(compiler), "build", native_arg(source), native_arg(built)], records)
    generated = built.with_suffix(".ll")
    assert_windows_llvm(generated.read_text(encoding="utf-8"), f"{name}/build")
    for opt in ("O0", "O2"):
        exe = work / f"{name}-{opt}.exe"
        command([clang, "-" + opt, "-std=c11", native_arg(generated), native_arg(runtime), "-o", native_arg(exe)], records)
        out, err, status = work / f"{opt}.stdout", work / f"{opt}.stderr", work / f"{opt}.status"
        evidence = {"variant": name, "optimization": opt, "exe": str(exe), "pe": parse_pe(exe),
                    **run_probe(probe, exe, out, err, status, records)}
        records.append(evidence)
        expected_status = OVERFLOW if name == "baseline" and opt == "O0" else 0
        if evidence["raw_exit_code"] != expected_status:
            failures.append(f"{name}/{opt}: expected {expected_status:#x}, recorded {evidence['raw_status_file']}")
        if expected_status == 0:
            if evidence["stdout"].replace("\r\n", "\n") != EXPECTED_INT16:
                failures.append(f"{name}/{opt}: stdout differs from exact fixture oracle")
            if evidence["stderr"]:
                failures.append(f"{name}/{opt}: stderr not empty: {evidence['stderr']!r}")
        print(json.dumps(evidence), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("baseline", "fixed", "baseline-runtime", "fixed-runtime", "baseline-source", "fixed-source", "probe", "output"):
        parser.add_argument("--" + arg, required=True, type=Path)
    args = parser.parse_args()
    if not WINDOWS:
        raise RuntimeError("this diagnostic must execute on Windows")
    for key, value in vars(args).items():
        setattr(args, key, value.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    records: list[dict] = []
    failures: list[str] = []
    report: dict = {"ok": False, "records": records, "failures": failures}
    try:
        for name in ("baseline", "fixed"):
            one_variant(name, getattr(args, name), getattr(args, name + "_runtime"),
                        getattr(args, name + "_source"), args.probe, args.output, records, failures)
        if failures:
            raise AssertionError("; ".join(failures))
        # The optimizer and backend fix must not change native stack settings.
        pe_settings = {(r["pe"]["stack_reserve"], r["pe"]["stack_commit"]) for r in records if "pe" in r}
        if len(pe_settings) != 1:
            raise AssertionError(f"baseline/fix PE stack settings differ: {pe_settings}")
        report["ok"] = True
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        (args.output / "windows-stack-diagnostic.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, subprocess.SubprocessError, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
