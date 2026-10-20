#!/usr/bin/env python3
"""Exact production pair roundtrip, not experimental adapter admission."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

TEST_DIR = Path(__file__).resolve().parent
ROOT = TEST_DIR.parents[1]
SOURCE = TEST_DIR / "fixtures/revise_pair_i32_roundtrip.cat"
WINDOWS = os.name == "nt" or sys.platform.startswith(("msys", "cygwin"))
RECORDS: list[dict] = []


def native_arg(path: Path | str) -> str:
    value = str(path)
    if WINDOWS and shutil.which("cygpath"):
        return subprocess.check_output(["cygpath", "-w", value], text=True).strip()
    return value


def executable_arg(path: Path | str) -> str:
    value = str(path)
    if WINDOWS and os.name != "nt" and shutil.which("cygpath"):
        return subprocess.check_output(["cygpath", "-u", value], text=True).strip()
    return native_arg(value) if os.name == "nt" else value


def compiler_path() -> str:
    if configured := os.environ.get("CLAW_EXE"):
        return executable_arg(configured)
    for candidate in (ROOT / "build/claw", ROOT / "build-ucrt64-clang/claw.exe",
                      ROOT / "build-ucrt64-clang/claw-codex.exe"):
        if candidate.is_file():
            return executable_arg(candidate)
    raise RuntimeError("missing compiler executable; set CLAW_EXE")


def run(args: list[str], expected: int = 0) -> subprocess.CompletedProcess:
    result = subprocess.run(args, capture_output=True, timeout=90)
    RECORDS.append({"argv": args, "exit": result.returncode,
                    "stdout": result.stdout.decode(errors="replace"),
                    "stderr": result.stderr.decode(errors="replace")})
    if result.returncode != expected:
        raise RuntimeError(f"unexpected exit (expected {expected}): {RECORDS[-1]}")
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def inspect_ir(text: str, label: str, *, effective: bool = False) -> dict:
    # Inspect actual target IR, never a singleton layout oracle or host C ABI.
    triple = re.search(r'^target triple = "([^"]+)"', text, re.M)
    require(triple is not None, f"{label}: missing actual target triple")
    target = triple.group(1)
    require("x86_64" in target and ("windows" in target if WINDOWS else "linux" in target),
            f"{label}: not the executing native target: {target}")
    layout = re.search(r'^target datalayout = "([^"]+)"', text, re.M)
    if effective:
        require(layout is not None and bool(layout.group(1)),
                f"{label}: native Clang did not expose effective DataLayout")
    pair = r"\{ i32, i32 \}"
    prefix = re.escape(SOURCE.stem)
    # Both raw production and effective Clang IR must retain Direct internal ABI.
    for pattern in (
        rf'define internal {pair} @"{prefix}::make"\(i32 [^,]+, i32 [^)]+\)',
        rf'define internal i32 @"{prefix}::take"\({pair} [^)]+\)',
        rf'call {pair} @"{prefix}::make"\(i32 40, i32 2\)',
        rf'call i32 @"{prefix}::take"\({pair} [^)]+\)',
    ):
        require(re.search(pattern, text) is not None, f"{label}: missing direct pair signature/call: {pattern}")
    return {"triple": target, "datalayout": layout.group(1) if layout else None}


def main(artifacts: Path) -> None:
    compiler = compiler_path()
    clang = shutil.which("clang")
    require(clang is not None, "missing native clang")
    source_bytes = SOURCE.read_bytes()
    evidence.update({"platform": sys.platform, "source": str(SOURCE),
                     "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
                     "compiler": compiler, "clang": clang,
                     "scope": "production native only; no adapter admission or numeric layout equivalence"})
    run([clang, "--version"])
    for stage in ("check", "oir", "lir", "llvm"):
        result = run([compiler, stage, native_arg(SOURCE)])
        (artifacts / f"{stage}.txt").write_bytes(result.stdout)
    lir = (artifacts / "lir.txt").read_text()
    prefix = SOURCE.stem
    for fragment in (
        f'lir.fn make(x: Int32 [scalar], y: Int32 [scalar]) -> Tuple[Int32, Int32] [direct] [abi=claw link=internal symbol={prefix}::make ffi=false]',
        f'lir.fn take(p: Tuple[Int32, Int32] [direct]) -> Int32 [scalar] [abi=claw link=internal symbol={prefix}::take ffi=false]',
        'alloc stack val %t0 : Tuple[Int32, Int32]',
        'store_field %t0.0 <- x : Int32 [proven-safe]',
        'store_field %t0.1 <- y : Int32 [proven-safe]',
        'return %t0 : Tuple[Int32, Int32]',
        'field p.0 : Int32 [proven-safe]',
        'field p.1 : Int32 [proven-safe]',
        'call.direct make(40, 2) : Tuple[Int32, Int32]',
        'call.direct take(p) : Int32',
    ):
        require(fragment in lir, f"LIR missing exact constructor/Direct pair contract: {fragment}")
    positions = [lir.index(fragment) for fragment in (
        'alloc stack val %t0 : Tuple[Int32, Int32]',
        'store_field %t0.0 <- x', 'store_field %t0.1 <- y',
        'return %t0 : Tuple[Int32, Int32]')]
    require(positions == sorted(positions), "pair fields not initialized before return")

    evidence["ir"] = {}
    # Production IR has no DataLayout today. Obtain it from the executing native
    # Clang for EACH real raw module; do not insert a guessed target/layout.
    def inspect_module(raw: Path, label: str) -> None:
        effective = artifacts / f"{label}-effective.ll"
        raw_info = inspect_ir(raw.read_text(), label)
        run([clang, "-S", "-emit-llvm", "-O0", "-x", "ir", native_arg(raw),
             "-o", native_arg(effective)])
        effective_info = inspect_ir(effective.read_text(), label + "/effective", effective=True)
        # Clang may canonicalize the vendor (unknown -> pc) on native Linux.
        def target_identity(triple: str) -> tuple:
            parts = triple.split("-")
            return (parts[0], *parts[2:])
        require(target_identity(raw_info["triple"]) == target_identity(effective_info["triple"]),
                f"{label}: native Clang changed the production target")
        evidence["ir"][label] = {"raw": raw_info, "effective": effective_info}

    inspect_module(artifacts / "llvm.txt", "llvm-stage")
    for opt, flags in (("O0", []), ("O2", ["--release"])):
        exe = artifacts / f"pair-{opt}.exe"
        run([compiler, "build", *flags, native_arg(SOURCE), native_arg(exe)])
        inspect_module(exe.with_suffix(".ll"), opt)
        magic = exe.read_bytes()[:4]
        require(magic.startswith(b"MZ") if WINDOWS else magic == b"\x7fELF",
                f"{opt}: wrong native executable format")
        result = run([executable_arg(exe)], expected=42)
        require(result.stdout == b"" and result.stderr == b"",
                f"{opt}: expected byte-exact empty stdout/stderr")
        print(f"[pair-i32/pass] {opt}: native {'PE/UCRT' if WINDOWS else 'ELF'} exit42 stdout=empty stderr=empty; constructor fields0/1, Direct internal pair; actual raw/effective target IR", flush=True)
    require(SOURCE.read_bytes() == source_bytes, "fixture changed during test")
    evidence["status"] = "PASS"
    print(f"[pair-i32/source] sha256={evidence['source_sha256']}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, help="fresh directory for this run")
    args = parser.parse_args()
    if args.artifacts:
        artifacts = args.artifacts.resolve()
        artifacts.mkdir(parents=True, exist_ok=True)
        require(not any(artifacts.iterdir()), "refusing to overwrite existing pair evidence; use a fresh --artifacts directory")
    else:
        parent = TEST_DIR / "artifacts/pair-i32-roundtrip"
        parent.mkdir(parents=True, exist_ok=True)
        artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    evidence: dict = {"status": "FAIL", "commands": RECORDS}
    try:
        main(artifacts)
    except Exception as error:
        evidence["error"] = str(error)
        raise
    finally:
        # Do not overwrite prior evidence on a rejected directory reuse.
        record = artifacts / "evidence.json"
        if not record.exists():
            record.write_text(json.dumps(evidence, indent=2) + "\n")
