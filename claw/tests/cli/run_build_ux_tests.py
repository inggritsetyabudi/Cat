#!/usr/bin/env python3
"""Black-box tests for the native build progress and diagnostic contract."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FIXTURES = HERE.parent / "frontend" / "fixtures"
WINDOWS_NATIVE = os.name == "nt" or sys.platform.startswith(("msys", "cygwin"))


def native_arg(path: Path) -> str:
    """Spell argv paths natively when Python is an MSYS/Cygwin interpreter."""
    if os.name != "nt" and WINDOWS_NATIVE:
        return subprocess.run(["cygpath", "-w", str(path)], capture_output=True,
                              text=True, check=True, timeout=10).stdout.strip()
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


CLAW = compiler()


def fail(message: str, result: subprocess.CompletedProcess[str] | None = None) -> None:
    if result is not None:
        message += f"\nstdout={result.stdout!r}\nstderr={result.stderr!r}"
    raise AssertionError(message)


def run(*args: str | Path, env: dict[str, str] | None = None,
        executable: Path = CLAW) -> subprocess.CompletedProcess[str]:
    argv = [native_arg(executable)] + [native_arg(a) if isinstance(a, Path) else str(a) for a in args]
    # Keep executable= POSIX-spelled for MSYS Python, while native-spelling argv.
    return subprocess.run(argv, executable=str(executable), text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, check=False, timeout=120)


def assert_build(result: subprocess.CompletedProcess[str], output: Path, profile: str) -> None:
    if result.returncode != 0:
        fail("native build failed", result)
    if result.stdout != "":
        fail("build status leaked onto stdout", result)
    if not output.is_absolute() or not output.exists():
        fail(f"build did not create absolute output {output}", result)
    if "\r" in result.stderr or "\x1b" in result.stderr:
        fail("redirected status contains carriage returns or ANSI escapes", result)
    summary = rf"^Finished native release \[{re.escape(profile)}\] in [0-9]+(?:\.[0-9]+)?s$"
    if not re.search(summary, result.stderr, re.MULTILINE):
        fail(f"missing timed {profile} completion summary", result)
    if f"Output {native_arg(output)}" not in result.stderr:
        fail("completion summary did not report the absolute output path", result)


def assert_stage_contract(stderr: str, modules: tuple[str, ...]) -> None:
    if not re.search(r"^\[[^\n]*\]\s*0/6\s+Resolving\b", stderr, re.MULTILINE):
        raise AssertionError(f"progress did not begin at truthful 0/6: {stderr!r}")
    stages = ("Resolving", "Checking", "Ownership", "Lowering", "Emitting", "Linking")
    positions = []
    for stage in stages:
        match = re.search(rf"^.*\b{stage}\b.*$", stderr, re.MULTILINE)
        if not match:
            raise AssertionError(f"missing exact build stage {stage!r}: {stderr!r}")
        positions.append(match.start())
    if positions != sorted(positions):
        raise AssertionError(f"build stages are out of order: {stderr!r}")
    for module in modules:
        if module not in stderr:
            raise AssertionError(f"stage status omitted module label {module!r}")
    if re.search(r"\b(?:Lower|Emit|Link)\b(?!ing)", stderr):
        raise AssertionError("stage status used truncated stage names")


def assert_no_override(stderr: str) -> None:
    """Real native builds must not need Clang's module-triple override suppression."""
    if "override-module" in stderr.lower() or "overriding the module target triple" in stderr.lower():
        raise AssertionError(f"native build emitted override-module message/diagnostic flag: {stderr!r}")


def assert_native_target(argv: list[str], flag: str, ir_path: Path) -> None:
    if sys.platform.startswith("linux") or WINDOWS_NATIVE:
        targets = [arg for arg in argv if arg.startswith("--target=")]
        if len(targets) != 1:
            raise AssertionError(f"native Clang must receive exactly one --target= argument: {argv!r}")
        ir_paths = [arg for arg in argv if arg.lower().endswith(".ll")]
        if ir_paths != [native_arg(ir_path)]:
            raise AssertionError(f"native Clang argv did not identify the generated IR file: {argv!r}")
        # Use the interpreter-native path for I/O, not the Win32 argv spelling.
        match = re.search(r'^target triple = "([^"]+)"$', ir_path.read_text(encoding="utf-8"), re.MULTILINE)
        if not match or targets[0] != "--target=" + match.group(1):
            raise AssertionError(f"native target did not match generated IR triple: {argv!r}")
    options = [arg for arg in argv if re.fullmatch(r"-O[0-9sz]", arg)]
    if options != [flag]:
        raise AssertionError(f"native profile sent incorrect optimization flags: {argv!r}")


def run_sentinel_builds(root: Path) -> None:
    """Use a copied compiler/runtime fixture to prove genuine Clang warnings survive."""
    sentinel_dir = root / "sentinel compiler"
    runtime_dir = sentinel_dir / "runtime"
    runtime_dir.mkdir(parents=True)
    sentinel_exe = sentinel_dir / CLAW.name
    shutil.copy2(CLAW, sentinel_exe)
    runtime = ROOT / "claw" / "runtime" / "native_runtime.c"
    (runtime_dir / "native_runtime.c").write_text(
        "#warning claw-native-target-diagnostic-sentinel\n" + runtime.read_text(encoding="utf-8"),
        encoding="utf-8")
    single = FIXTURES / "revise_maybe.cat"
    for args, profile in [((), "debug + unoptimized"), (("--release",), "optimized")]:
        output = root / ("sentinel-" + ("debug" if not args else "release") + ".exe")
        result = run("build", *args, single, output, executable=sentinel_exe)
        assert_build(result, output, profile)
        if "claw-native-target-diagnostic-sentinel" not in result.stderr or "warning:" not in result.stderr:
            fail("genuine Clang sentinel warning was not preserved", result)
        if sys.platform.startswith("linux") or WINDOWS_NATIVE:
            assert_no_override(result.stderr)
        executed = subprocess.run([native_arg(output)], executable=str(output),
                                  capture_output=True, text=True, timeout=30)
        if executed.returncode != 0 or executed.stdout.replace("\r\n", "\n") != "42\n0\nC@\nworld\n":
            fail("Clang sentinel changed native executable behavior", executed)


def verify_native_flags(root: Path, single: Path) -> None:
    """Record native argv with a compiled, platform-native fake Clang."""
    fake_dir = root / "fake clang"
    fake_dir.mkdir()
    c_source = root / "record_clang.c"
    c_source.write_text(r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    FILE *log = fopen(getenv("CLAW_ARG_LOG"), "wb");
    if (!log) return 90;
    for (int i = 1; i < argc; ++i) fprintf(log, "%s\n", argv[i]);
    fclose(log);
    for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "-o")) {
        FILE *out = fopen(argv[i + 1], "wb");
        if (!out) return 91;
        return fclose(out) == 0 ? 0 : 92;
    }
    return 93;
}
''', encoding="utf-8")
    real_clang = shutil.which("clang")
    if not real_clang:
        raise AssertionError("native Clang is required for argv regression")
    fake = fake_dir / ("clang.exe" if WINDOWS_NATIVE else "clang")
    compiled = subprocess.run([native_arg(Path(real_clang)), native_arg(c_source), "-o", native_arg(fake)],
                              executable=real_clang, capture_output=True, text=True, timeout=60)
    if compiled.returncode:
        fail("could not compile native argv recorder", compiled)
    log = root / "clang-argv.log"
    env = dict(os.environ, CLAW_ARG_LOG=native_arg(log))
    env["PATH"] = str(fake_dir) + os.pathsep + os.environ["PATH"]
    for flag, args in [("-O0", ()), ("-O2", ("--release",))]:
        output = root / (flag + " output.exe")
        result = run("build", *args, single, output, env=env)
        assert_build(result, output, "optimized" if args else "debug + unoptimized")
        actual = log.read_text(encoding="utf-8").splitlines()
        assert_native_target(actual, flag, output.with_suffix(".ll"))


def main() -> int:
    single = FIXTURES / "revise_maybe.cat"
    workspace = FIXTURES / "revise_nested_scoped_workspace"
    with tempfile.TemporaryDirectory(prefix="claw-build-ux-") as directory:
        root = Path(directory)
        default_output = root / "default.exe"
        default = run("build", single, default_output)
        assert_build(default, default_output, "debug + unoptimized")
        assert_stage_contract(default.stderr, ("revise_maybe::main",))
        if sys.platform.startswith("linux") or WINDOWS_NATIVE:
            assert_no_override(default.stderr)
        if "-O0" not in default.stderr and "unoptimized" not in default.stderr:
            fail("default build was not reported as unoptimized", default)
        warning_source = root / "warning with spaces.cat"
        warning_source.write_text("/// A documented private entry produces a real Cat warning.\nfn main() -> Int32 { return 0 }\n", encoding="utf-8")
        warning_result = run("build", warning_source, root / "warning output.exe")
        assert_build(warning_result, root / "warning output.exe", "debug + unoptimized")
        warning = warning_result.stderr.find("warning:")
        ownership_stage = warning_result.stderr.find("Ownership")
        if warning < 0 or ownership_stage < warning:
            fail("Cat warning did not print before safe ownership/codegen continuation", warning_result)

        release_output = root / "release.exe"
        release = run("build", "--release", single, release_output)
        assert_build(release, release_output, "optimized")
        assert_stage_contract(release.stderr, ("revise_maybe::main",))
        if sys.platform.startswith("linux") or WINDOWS_NATIVE:
            assert_no_override(release.stderr)
        if "-O2" not in release.stderr and "optimized" not in release.stderr:
            fail("--release build was not reported as optimized", release)

        workspace_output = root / "workspace.exe"
        workspace_result = run("build", workspace, workspace_output)
        assert_build(workspace_result, workspace_output, "debug + unoptimized")
        assert_stage_contract(workspace_result.stderr, ("main::main", "main::src::nested"))
        if sys.platform.startswith("linux") or WINDOWS_NATIVE:
            assert_no_override(workspace_result.stderr)
        workspace_release_output = root / "workspace-release.exe"
        workspace_release = run("build", "--release", workspace, workspace_release_output)
        assert_build(workspace_release, workspace_release_output, "optimized")
        if sys.platform.startswith("linux") or WINDOWS_NATIVE:
            assert_no_override(workspace_release.stderr)

        invalid = run("build", "--not-a-real-build-flag", single, root / "bad.exe")
        if invalid.returncode == 0 or invalid.stdout:
            fail("invalid build flags changed failure stream contract", invalid)
        if "Finished native release" in invalid.stderr or "Output " in invalid.stderr:
            fail("invalid build flags produced a success summary", invalid)

        parse = run("build", FIXTURES / "revise_bad_char_empty.cat", root / "bad-source.exe")
        if parse.returncode == 0 or parse.stdout:
            fail("source parse failure changed build stream contract", parse)
        if "error[parse]" in parse.stderr:
            fail("compact source diagnostics retained a stage prefix", parse)
        if "error: Character literal must contain exactly one Unicode scalar value." not in parse.stderr:
            fail("compact source diagnostic text is missing", parse)

        ownership = run("build", FIXTURES / "revise_bad_str_branch_owner_move.cat", root / "bad-owner.exe")
        if ownership.returncode == 0 or ownership.stdout:
            fail("ownership failure changed build stream contract", ownership)
        if "error[ownership]:" in ownership.stderr or "error: Cannot move `first`" not in ownership.stderr:
            fail("compact ownership diagnostic header is incorrect", ownership)
        if "borrow starts here" not in ownership.stderr or "move occurs here" not in ownership.stderr:
            fail("ownership diagnostic omitted borrow-start and move-occurs labels", ownership)
        if "revise_bad_str_branch_owner_move.cat" not in ownership.stderr or "^" not in ownership.stderr:
            fail("ownership diagnostic omitted source path/snippet", ownership)
        if "revise_bad_str_branch_owner_move.cat:8:17" not in ownership.stderr or "revise_bad_str_branch_owner_move.cat:4:" not in ownership.stderr:
            fail("ownership labels do not identify the actual move and original borrow lines", ownership)
        if not re.search(r"error: build failed for `revise_bad_str_branch_owner_move`", ownership.stderr):
            fail("failed build omitted its final named failure status", ownership)

        # Check executable behavior, not just that Clang created a pathname.
        executed = subprocess.run([native_arg(default_output)], executable=str(default_output),
                                  capture_output=True, text=True, timeout=30)
        if executed.returncode != 0 or executed.stdout.replace("\r\n", "\n") != "42\n0\nC@\nworld\n":
            fail("default native program behavior changed", executed)
        for output, expected in [(release_output, "42\n0\nC@\nworld\n"),
                                 (workspace_output, "workspace\n")]:
            executed = subprocess.run([native_arg(output)], executable=str(output),
                                      capture_output=True, text=True, timeout=30)
            if executed.returncode != 0 or executed.stdout.replace("\r\n", "\n") != expected:
                fail(f"native program behavior changed for {output.name}", executed)
        for argv in [("build", single, release_output, "--release"),
                     ("build-native", "--release", single, release_output)]:
            assert_build(run(*argv), release_output, "optimized")
        for argv in [("build",), ("build", "--release"),
                     ("build", single, root / "one.exe", root / "extra.exe")]:
            result = run(*argv)
            if result.returncode != 1 or result.stdout or "error: build failed for" not in result.stderr:
                fail("invalid build usage must fail on stderr only", result)
        # Legacy non-build fourth positional argument was accepted and ignored.
        check = run("check", single, "ignored-output")
        if check.returncode != 0 or not check.stdout.startswith("Check passed for module:"):
            fail("unrelated check command output/argument behavior changed", check)
        verify_native_flags(root, single)
        if sys.platform.startswith("linux") or WINDOWS_NATIVE:
            run_sentinel_builds(root)

    print("[build-ux/pass] native build UX contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(f"build UX test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
