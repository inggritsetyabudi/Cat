#!/usr/bin/env python3
"""Integration checks for the opt-in transitional LLVM object native backend."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
from typing import Any

HERE = Path(__file__).resolve().parent
CLAW_ROOT = HERE.parents[2]
FIXTURES = HERE / "fixtures"
NEGATIVE = HERE / ".." / "frontend" / "fixtures" / "revise_bad_str_borrowed_field.cat"

CASES = {
    "simple": (FIXTURES / "revise_single_file.cat", "3\n"),
    "tuple": (
        FIXTURES / "revise_tuples.cat",
        "42\n42\n42\n42\n42\n42\n42\nfloat-ok\n",
    ),
    "fixed-array": (
        FIXTURES / "revise_fixed_array.cat",
        "1\n2\n3\n10\n20\n30\n10\narray-once\nindex-once\n8\n10\n20\n30\n0\n10\n1\n20\n2\n30\narray-once\n7\n8\n9\n",
    ),
    "string-try": (FIXTURES / "revise_try_result_shadowing.cat", "7\n99\ninner\n0\n"),
}


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser()
    p.add_argument("--compiler", required=True, type=Path)
    p.add_argument("--baseline", type=Path, help="legacy compiler used for the no-backend default check")
    p.add_argument("--artifacts", required=True, type=Path)
    p.add_argument("--disabled-compiler", type=Path)
    return p


class TestRun:
    def __init__(self, artifacts: Path) -> None:
        self.artifacts = artifacts
        self.records: list[dict[str, Any]] = []
        artifacts.mkdir(parents=True, exist_ok=True)

    def run(self, argv: list[object], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        command = [str(x) for x in argv]
        try:
            result = subprocess.run(
                command,
                cwd=str(cwd or self.artifacts),
                env=env,
                shell=False,
                timeout=180,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except (OSError, subprocess.SubprocessError) as exc:
            self.records.append({"argv": command, "error": repr(exc)})
            raise
        self.records.append({
            "argv": command,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "cwd": str(cwd or self.artifacts),
        })
        return result

    def save(self) -> None:
        (self.artifacts / "llvm-object-test-results.json").write_text(
            json.dumps(self.records, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )


def compiler_path(path: Path) -> Path:
    path = path.resolve()
    if not path.is_file() or (os.name != "nt" and not os.access(path, os.X_OK)):
        raise AssertionError(f"compiler is not executable: {path}")
    return path


def object_path(output: Path) -> Path:
    return Path(str(output) + ".cat.o")


def remove_outputs(output: Path) -> None:
    for path in (output, output.with_suffix(".ll"), object_path(output)):
        path.unlink(missing_ok=True)


def assert_artifacts(output: Path) -> None:
    ll = output.with_suffix(".ll")
    obj = object_path(output)
    assert output.is_file(), f"missing executable: {output}"
    assert ll.is_file(), f"missing preserved LLVM IR: {ll}"
    assert obj.is_file(), f"missing LLVM object: {obj}"
    data = obj.read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01", f"object is not little-endian ELF64: {obj}"
    assert int.from_bytes(data[16:18], "little") == 1, f"object is not ELF relocatable: {obj}"
    assert int.from_bytes(data[18:20], "little") == 62, f"object is not x86_64: {obj}"
    text = ll.read_text(encoding="utf-8")
    assert "target datalayout" in text, f"LLVM IR has no DataLayout: {ll}"
    assert "define i32 @main" in text, f"LLVM IR has no native main wrapper: {ll}"


def execute(runner: TestRun, output: Path, expected: str, label: str) -> None:
    result = runner.run([output])
    stdout = result.stdout.replace("\r\n", "\n").replace("\r", "\n")
    assert result.returncode == 0, f"{label} exited {result.returncode}: {result.stdout}{result.stderr}"
    assert stdout == expected, f"{label} output mismatch: {stdout!r} != {expected!r}"


def build_object(runner: TestRun, claw: Path, source: Path, output: Path, *, release: bool = False, alias: str = "build", env: dict[str, str] | None = None) -> None:
    remove_outputs(output)
    args: list[object] = [claw, alias, "--native-backend=llvm-object"]
    if release:
        args.append("--release")
    args.extend([source, output])
    result = runner.run(args, env=env)
    assert result.returncode == 0, f"{alias} llvm-object failed: {result.stdout}{result.stderr}"
    assert_artifacts(output)


def assert_rejected(
    runner: TestRun,
    args: list[object],
    output: Path,
    label: str,
    *,
    diagnostic: str | tuple[str, ...],
    env: dict[str, str] | None = None,
) -> None:
    remove_outputs(output)
    result = runner.run(args, env=env)
    assert result.returncode != 0, f"{label} unexpectedly succeeded"
    diagnostics = result.stdout + result.stderr
    expected = (diagnostic,) if isinstance(diagnostic, str) else diagnostic
    for substring in expected:
        assert substring in diagnostics, f"{label} missing diagnostic {substring!r}: {diagnostics}"
    assert not output.exists() and not output.with_suffix(".ll").exists() and not object_path(output).exists(), f"{label} emitted artifacts"


def make_clang_spy(artifacts: Path) -> tuple[Path, Path, Path]:
    real = shutil.which("clang")
    if not real:
        raise AssertionError("clang is required for the external-linker spy")
    directory = artifacts / "clang-spy"
    directory.mkdir(parents=True, exist_ok=True)
    log = directory / "argv.jsonl"
    log.write_text("", encoding="utf-8")
    wrapper = directory / "clang"
    wrapper.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, sys\n"
        f"log = {str(log)!r}\n"
        "with open(log, 'a', encoding='utf-8') as f: f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
        f"os.execv({str(Path(real))!r}, [" + repr(str(Path(real))) + "] + sys.argv[1:])\n",
        encoding="utf-8",
    )
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
    return directory, log, Path(real)


def main() -> int:
    if sys.platform != "linux" or os.uname().machine not in {"x86_64", "amd64"}:
        print("SKIP: LLVM object backend tests require Linux x86_64")
        return 0
    args = parser().parse_args()
    claw = compiler_path(args.compiler)
    baseline = compiler_path(args.baseline) if args.baseline else claw
    artifacts = args.artifacts.resolve()
    runner = TestRun(artifacts)
    work = artifacts / "inputs"
    work.mkdir(parents=True, exist_ok=True)
    try:
        # Debug and release exercise O0/O2 object emission and exact runtime results.
        for release in (False, True):
            mode = "release" if release else "debug"
            for name, (source, expected) in CASES.items():
                output = artifacts / f"llvm-object-{mode}-{name}.exe"
                build_object(runner, claw, source, output, release=release)
                execute(runner, output, expected, f"{mode}/{name}")

        # Public alias is equivalent to build for the opt-in backend.
        alias_output = artifacts / "llvm-object-alias.exe"
        build_object(runner, claw, CASES["simple"][0], alias_output, alias="build-native")
        execute(runner, alias_output, CASES["simple"][1], "build-native")

        # Explicit clang remains legacy and must not create the transitional object.
        legacy = artifacts / "legacy-clang.exe"
        remove_outputs(legacy)
        result = runner.run([claw, "build", "--native-backend=clang", CASES["simple"][0], legacy])
        assert result.returncode == 0, f"explicit clang backend failed: {result.stdout}{result.stderr}"
        assert legacy.is_file() and legacy.with_suffix(".ll").is_file() and not object_path(legacy).exists()
        execute(runner, legacy, CASES["simple"][1], "explicit clang")

        # No backend flag preserves the default legacy behavior; it must not opt in.
        default = artifacts / "legacy-default.exe"
        remove_outputs(default)
        result = runner.run([claw, "build", CASES["simple"][0], default])
        assert result.returncode == 0, f"default backend failed: {result.stdout}{result.stderr}"
        assert default.is_file() and default.with_suffix(".ll").is_file() and not object_path(default).exists()
        execute(runner, default, CASES["simple"][1], "default backend")

        # If supplied, the immutable legacy compiler is only a no-flag differential check.
        if args.baseline:
            baseline_default = artifacts / "baseline-default.exe"
            remove_outputs(baseline_default)
            result = runner.run([baseline, "build", CASES["simple"][0], baseline_default])
            assert result.returncode == 0, f"baseline default backend failed: {result.stdout}{result.stderr}"
            assert baseline_default.is_file() and baseline_default.with_suffix(".ll").is_file() and not object_path(baseline_default).exists()
            execute(runner, baseline_default, CASES["simple"][1], "baseline default backend")

        # Unknown backend and a compiler built without the option must fail, never fall back.
        unknown = artifacts / "unknown.exe"
        assert_rejected(
            runner,
            [claw, "build", "--native-backend=not-a-backend", CASES["simple"][0], unknown],
            unknown,
            "unknown backend",
            diagnostic="unknown build option --native-backend=not-a-backend",
        )
        if args.disabled_compiler:
            disabled = compiler_path(args.disabled_compiler)
            disabled_output = artifacts / "disabled.exe"
            assert_rejected(
                runner,
                [disabled, "build", "--native-backend=llvm-object", CASES["simple"][0], disabled_output],
                disabled_output,
                "disabled LLVM backend",
                diagnostic=("LLVM object backend is not enabled", "No fallback was attempted"),
            )
        else:
            runner.records.append({"case": "disabled compiler", "skipped": True, "reason": "--disabled-compiler not supplied"})

        # Frontend/ownership failure must happen before any native artifact is emitted.
        negative_out = artifacts / "negative-borrowed-shape.exe"
        assert_rejected(
            runner,
            [claw, "build", "--native-backend=llvm-object", NEGATIVE, negative_out],
            negative_out,
            "borrowed ordinary-shape negative",
            diagnostic="Hint: use `String` for owned text",
        )

        # Spaces, '$', ';', and marker text are argv data, not shell syntax.
        spaced_source = work / "source $; marker with spaces.cat"
        spaced_output = artifacts / "output $; marker with spaces.exe"
        shutil.copyfile(CASES["simple"][0], spaced_source)
        spy_dir, spy_log, real_clang = make_clang_spy(artifacts)
        env = os.environ.copy()
        env["PATH"] = str(spy_dir) + os.pathsep + env.get("PATH", "")
        build_object(runner, claw, spaced_source, spaced_output, env=env)
        execute(runner, spaced_output, CASES["simple"][1], "spaced paths")
        assert spy_log.is_file(), "clang spy did not run"
        invocations = [json.loads(line) for line in spy_log.read_text(encoding="utf-8").splitlines() if line.strip()]
        assert len(invocations) == 1, f"expected one external runtime/link step, got: {invocations}"
        flat = [item for invocation in invocations for item in invocation]
        assert any(item.endswith("native_runtime.c") for item in flat), f"runtime source was not passed to clang: {invocations}"
        assert "--target=x86_64-unknown-linux-gnu" in flat, f"clang target was not explicit: {invocations}"
        assert str(object_path(spaced_output)) in flat, f"linker did not receive the current object's argv: {invocations}"
        assert not any(item.endswith(".ll") for item in flat), f"linker unexpectedly received LLVM IR: {invocations}"
        assert str(real_clang) not in flat, "spy wrapper was not used"

        # A link failure is not permission to retry with textual LLVM or report
        # success. Inspectable IR/object remain available for diagnosis.
        failure_dir = artifacts / "failing-linker"
        failure_dir.mkdir(exist_ok=True)
        failing_clang = failure_dir / "clang"
        failing_clang.write_text("#!/bin/sh\necho forced-link-failure >&2\nexit 91\n", encoding="utf-8")
        failing_clang.chmod(0o700)
        failure_env = os.environ.copy()
        failure_env["PATH"] = str(failure_dir) + os.pathsep + failure_env.get("PATH", "")
        failed_output = artifacts / "link-failed.exe"
        remove_outputs(failed_output)
        failed = runner.run([claw, "build", "--native-backend=llvm-object", CASES["simple"][0], failed_output], env=failure_env)
        assert failed.returncode != 0 and "forced-link-failure" in failed.stderr
        assert "exited with status 91" in failed.stderr and "Finished native" not in failed.stderr
        assert not failed_output.exists(), "link failure produced an executable"
        assert failed_output.with_suffix(".ll").is_file() and object_path(failed_output).is_file()
    finally:
        runner.save()
    print(f"[llvm-object/pass] results recorded in {artifacts / 'llvm-object-test-results.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, subprocess.SubprocessError) as exc:
        print(f"[llvm-object/fail] {exc}", file=sys.stderr)
        raise SystemExit(1)
