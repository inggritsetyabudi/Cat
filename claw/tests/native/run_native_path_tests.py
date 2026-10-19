#!/usr/bin/env python3
"""Regression tests for native builds when compiler and project paths contain spaces."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ARTIFACTS = HERE / "artifacts" / "native path regression"
RUNTIME = ROOT / "claw" / "runtime" / "native_runtime.c"
SOURCE_TEXT = 'fn main() -> Int32 {\n    println("native-path-ok")\n    return 7\n}\n'
EXPECTED = "native-path-ok\n"
WINDOWS_NATIVE = os.name == "nt" or sys.platform.startswith(("msys", "cygwin"))


def native_arg(path: Path) -> str:
    """Use native Windows spelling when Python is running under MSYS/Cygwin."""
    if os.name != "nt" and WINDOWS_NATIVE:
        converted = subprocess.run(
            ["cygpath", "-w", str(path)], capture_output=True, text=True, check=True, timeout=10
        )
        return converted.stdout.strip()
    return str(path)


def compiler() -> Path:
    configured = os.environ.get("CLAW_EXE")
    candidates = [Path(configured)] if configured else [
        ROOT / "claw" / "build" / "claw",
        ROOT / "claw" / "build-ucrt64-clang" / "claw.exe",
        ROOT / "claw" / "build-ucrt64-clang" / "claw-codex.exe",
    ]
    for candidate in candidates:
        if candidate.is_file() and (os.name == "nt" or os.access(candidate, os.X_OK)):
            return candidate.resolve()
    raise AssertionError("missing compiler executable; set CLAW_EXE")


def run(args: list[object], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    # MSYS Python resolves the executable and cwd through its POSIX filesystem.
    # Only argv belongs to the native child (including argv[0], used to find the
    # bundled runtime). A Windows-spelled executable= path fails MSYS execvp.
    return subprocess.run(
        [native_arg(Path(a)) if isinstance(a, Path) else str(a) for a in args],
        executable=str(args[0]), cwd=str(cwd),
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        shell=False, timeout=120,
    )


def remove_target(path: Path) -> None:
    path.unlink(missing_ok=True)
    path.with_suffix(".ll").unlink(missing_ok=True)


def assert_build(compiler_path: Path, command: str, source: Path, output: Path, cwd: Path) -> None:
    remove_target(output)
    result = run([compiler_path, command, source, output], cwd=cwd)
    if result.returncode:
        raise AssertionError(f"{command} failed ({result.returncode}):\n{result.stdout}")
    ll = output.with_suffix(".ll")
    if not output.is_file() or not ll.is_file():
        raise AssertionError(f"{command} omitted expected .exe/.ll: {output}")
    executed = run([output], cwd=cwd)
    stdout = executed.stdout.replace("\r\n", "\n").replace("\r", "\n")
    if executed.returncode != 7 or stdout != EXPECTED:
        raise AssertionError(f"{command} execution mismatch: exit={executed.returncode}, output={stdout!r}")


def assert_default_build(compiler_path: Path, command: str, source: Path, output: Path, cwd: Path) -> None:
    remove_target(output)
    result = run([compiler_path, command, source], cwd=cwd)
    if result.returncode:
        raise AssertionError(f"{command} default output failed ({result.returncode}):\n{result.stdout}")
    if not output.is_file() or not output.with_suffix(".ll").is_file():
        raise AssertionError(f"{command} default output omitted expected .exe/.ll: {output}")
    executed = run([output], cwd=cwd)
    stdout = executed.stdout.replace("\r\n", "\n").replace("\r", "\n")
    if executed.returncode != 7 or stdout != EXPECTED:
        raise AssertionError(f"{command} default execution mismatch: exit={executed.returncode}, output={stdout!r}")


def main() -> None:
    # Resolve before any cwd changes: all subsequent invocations deliberately use unrelated cwd.
    original_compiler = compiler()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    source = ARTIFACTS / "input file with spaces.cat"
    source.write_text(SOURCE_TEXT, encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="claw-native-path-cwd-") as unrelated:
        unrelated_cwd = Path(unrelated)
        bundle = ARTIFACTS / "compiler bundle with spaces"
        bundle.mkdir(parents=True, exist_ok=True)
        copied_compiler = bundle / original_compiler.name
        shutil.copy2(original_compiler, copied_compiler)
        copied_compiler.chmod(copied_compiler.stat().st_mode | 0o111)
        copied_runtime = bundle / "runtime" / "native_runtime.c"
        copied_runtime.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(RUNTIME, copied_runtime)

        spaced_dir = ARTIFACTS / "output dir with spaces"
        spaced_dir.mkdir(parents=True, exist_ok=True)
        explicit = spaced_dir / "& marker; $ value.exe"
        assert_build(copied_compiler, "build", source, explicit, unrelated_cwd)

        # No explicit target: both public native-build spellings must choose their default safely.
        default_dir = source.parent / "build"
        for alias in ("build", "build-native"):
            default = default_dir / "input_file_with_spaces.exe"
            assert_default_build(copied_compiler, alias, source, default, unrelated_cwd)

        # A no-space control uses a separate staging tree and still leaves inspectable artifacts here.
        with tempfile.TemporaryDirectory(prefix="claw-native-path-control-") as control_temp:
            control = Path(control_temp)
            control_source = control / "control.cat"
            control_output = control / "control.exe"
            control_source.write_text(SOURCE_TEXT, encoding="utf-8")
            assert_build(original_compiler, "build", control_source, control_output, unrelated_cwd)
            shutil.copy2(control_output, ARTIFACTS / "control.exe")
            shutil.copy2(control_output.with_suffix(".ll"), ARTIFACTS / "control.ll")

    print("[native-path/pass] spaced input/output/compiler/runtime, build aliases, and no-space control")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
