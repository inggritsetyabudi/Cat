#!/usr/bin/env python3
"""Check CRT serialization everywhere and actual _wspawnvp round-trip on Windows."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ARTIFACTS = ROOT / "claw/tests/native/artifacts/argv probe with spaces"


def main() -> None:
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    executable = ARTIFACTS / "windows argv probe.exe"
    executable.unlink(missing_ok=True)
    subprocess.run([
        os.environ.get("CXX", "clang++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "claw/src"),
        str(Path(__file__).with_name("windows_argv_tests.cpp")),
        "-o", str(executable),
    ], check=True, timeout=90)
    subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
