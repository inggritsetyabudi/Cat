#!/usr/bin/env python3
"""CMake-level regression tests for the pinned LLVM SDK discovery contract."""
from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def configure(args, source_dir: Path, build_dir: Path, llvm_config: Path | None, enabled: bool):
    command = [args.cmake, "-S", str(source_dir), "-B", str(build_dir),
               f"-DCLAW_ENABLE_LLVM_CPP_POC={'ON' if enabled else 'OFF'}"]
    if args.generator:
        command += ["-G", args.generator]
    if args.cxx_compiler:
        command.append(f"-DCMAKE_CXX_COMPILER:FILEPATH={args.cxx_compiler}")
    if llvm_config is not None:
        command.append(f"-DCLAW_LLVM_CONFIG:FILEPATH={llvm_config}")
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)


def fake_llvm_config(path: Path, version: str) -> Path:
    if os.name == "nt":
        path = path.with_suffix(".cmd")
        path.write_text(f'@echo off\nif "%~1"=="--version" (echo {version}&exit /b 0)\necho unsupported llvm-config probe 1>&2\nexit /b 1\n')
    else:
        path.write_text(f'#!/bin/sh\nif [ "$1" = "--version" ]; then echo {version}; exit 0; fi\necho unsupported llvm-config probe >&2; exit 1\n')
        path.chmod(0o755)
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--generator")
    parser.add_argument("--cxx-compiler")
    args = parser.parse_args()
    source_dir = Path(args.source_dir).resolve()
    with tempfile.TemporaryDirectory(prefix="claw-llvm-sdk-tests-") as temp:
        root = Path(temp)
        # These are deliberate version-only fixtures, not claimed installations.
        for version in ("18.1.3", "23.1.4", "23.1.30"):
            config = fake_llvm_config(root / ("fake-config-" + version), version)
            result = configure(args, source_dir, root / ("reject-" + version), config, True)
            if result.returncode == 0 or "requires exact LLVM 23.1.3" not in result.stdout:
                print(f"SDK fixture {version} was not rejected as an exact-version mismatch:\n{result.stdout}")
                return 1
        # An explicit invalid SDK path must remain irrelevant when the option is OFF.
        off = configure(args, source_dir, root / "option-off-without-sdk", root / "not-installed", False)
        if off.returncode != 0:
            print(f"CLAW_ENABLE_LLVM_CPP_POC=OFF unexpectedly required an SDK:\n{off.stdout}")
            return 1
    print("PASS: reject 18.1.3, 23.1.4 and 23.1.30; option OFF requires no SDK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
