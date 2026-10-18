#!/usr/bin/env python3
"""Check target-directed float literals through semantics, LLVM and native execution."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TESTS = ROOT / "claw" / "tests"
FIXTURE = TESTS / "native" / "fixtures" / "revise_float_target_direct.cat"
ARTIFACTS = TESTS / "native" / "artifacts"
RUNTIME = ROOT / "claw" / "runtime" / "native_runtime.c"
SUCCESS = "float-target-direct-ok\n"


def compiler() -> str:
    explicit = os.environ.get("CLAW_EXE")
    candidates = [explicit] if explicit else [
        str(ROOT / "claw" / "build" / "claw"),
        str(ROOT / "claw" / "build-ucrt64-clang" / "claw.exe"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise AssertionError("missing compiler executable; set CLAW_EXE")


def run(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)


def require_success(result: subprocess.CompletedProcess[str], description: str) -> None:
    if result.returncode:
        raise AssertionError(f"{description} failed ({result.returncode}):\n{result.stdout}")


def promoted32(bits: int) -> str:
    value = struct.unpack(">f", struct.pack(">I", bits))[0]
    return "0X" + struct.pack(">d", value).hex().upper()


def main() -> None:
    claw = compiler()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    output = ARTIFACTS / "revise_float_target_direct.exe"
    ll = output.with_suffix(".ll")
    for path in (output, ll):
        path.unlink(missing_ok=True)
    require_success(run([claw, "check", str(FIXTURE)]), "positive semantic check")
    require_success(run([claw, "build", str(FIXTURE), str(output)]), "positive native build")
    assert output.is_file() and ll.is_file(), "native build omitted expected artifacts"
    native = run([str(output)])
    require_success(native, "positive native execution")
    assert native.stdout.replace("\r", "") == SUCCESS, repr(native.stdout)
    ir = ll.read_text().upper()
    expected32 = {
        "one32": 0x3F800000, "tie32": 0x3F800000,
        "above_tie32": 0x3F800001, "max32": 0x7F7FFFFF,
        "max32_boundary_below": 0x7F7FFFFF,
        "min_normal32": 0x00800000, "subnormal32": 0x00000001,
    }
    for binding, bits in expected32.items():
        pattern = rf"STORE FLOAT {promoted32(bits)}, PTR %{binding.upper()}\.ADDR"
        assert re.search(pattern, ir), f"wrong Float32 bits for {binding}: {promoted32(bits)}"
    for binding, bits in {"max64": "7FEFFFFFFFFFFFFF", "min_normal64": "0010000000000000",
                          "subnormal64": "0000000000000001", "rounded64": "3FF0000000000001"}.items():
        assert f"STORE DOUBLE 0X{bits}, PTR %{binding.upper()}.ADDR" in ir, binding
    # Unary minus must retain fneg on positive zero, not lose the sign while parsing.
    assert "FNEG FLOAT 0X0000000000000000" in ir, "missing Float32 signed zero"
    assert "FNEG DOUBLE 0X0000000000000000" in ir, "missing Float64 signed zero"
    assert "FCMP" in ir
    print("[float/pass] check, native, target bits, direct rounding and signed-zero IR")

    paths_fixture = FIXTURE.with_name("revise_float_literal_paths.cat")
    paths_output = ARTIFACTS / "revise_float_literal_paths.exe"
    paths_ll = paths_output.with_suffix(".ll")
    for path in (paths_output, paths_ll):
        path.unlink(missing_ok=True)
    require_success(run([claw, "check", str(paths_fixture)]), "literal paths semantic check")
    require_success(run([claw, "build", str(paths_fixture), str(paths_output)]), "literal paths build")
    paths_native = run([str(paths_output)])
    require_success(paths_native, "static/tuple/carrier/assignment native execution")
    assert paths_native.stdout == "", paths_native.stdout
    paths_ir = paths_ll.read_text().upper()
    assert "CONSTANT FLOAT 0X3FF4000000000000" in paths_ir, "incorrect static Float32"
    assert "CONSTANT DOUBLE 0X0000000000000001" in paths_ir, "incorrect static subnormal"
    print("[float/pass] static, tuple, unary, carrier, assignment, contextual if/pick")

    # Each case is a separate compilation: an earlier diagnostic cannot mask a bug.
    cases: dict[str, tuple[str, str]] = {}
    for width in (32, 64):
        for label, value in (("overflow", "1e9999"), ("underflow", "1e-9999")):
            cases[f"typed_{width}_{label}"] = (f"val value: Float{width} = {value}", f"does not fit target type Float{width}")
            cases[f"static_{width}_{label}"] = (f"static value: Float{width} = {value}", f"does not fit target type Float{width}")
            cases[f"suffix_{width}_{label}"] = (f"val value = {value}_Float{width}", f"does not fit target type Float{width}")
    for label, magnitude in (("overflow", "1e9999"), ("underflow", "1e-9999")):
        for sign in ("", "-"):
            value = sign + magnitude
            for context, body in {
                "inferred": f"val value = {value}",
                "discarded": value,
                "tuple": f"val values = ({value}, 1)",
                "if": f"val value = if true {{ {value} }} else {{ 1.0 }}",
                "pick": f"val route: Maybe[Bool] = Some(true)\nval value = pick route {{ Some(flag) {{ {value} }} None {{ 1.0 }} }}",
                "anchor": f"val value = Anchor.new({value})",
                "maybe": f"val value: Maybe[Float64] = Some({value})",
            }.items():
                cases[f"{context}_{'negative_' if sign else ''}{label}"] = (body, "does not fit target type Float64")
    cases.update({
        "overflow_boundary32": ("val value = 340282356779733661637539395458142568448.0_Float32", "does not fit target type Float32"),
        "separator": ("val value = 1.2__3_Float64", "Malformed float literal"),
        "suffix_mismatch": ("val value: Float64 = 1.0_Float32", "Initializer type mismatch"),
        "unsupported128": ("val value = 1.0_Float128", "suffix must be Float32 or Float64"),
        "float32_integer_precision": ("val value: Float32 = 16777217", "does not fit exactly"),
        # No new cross-branch inference or implicit Float32->Float64 conversion is introduced.
        "mixed_width": ("val value: Float64 = 1.0_Float32 + 1.0", "Incompatible arithmetic operands"),
        "mixed_branch": ("val value = if true { 1.0_Float32 } else { 2.0 }", "Incompatible branch types"),
    })
    with tempfile.TemporaryDirectory(prefix="claw-float-negative-") as temp:
        for name, (body, diagnostic) in cases.items():
            source = Path(temp) / (name + ".cat")
            target = source.with_suffix(".exe")
            source.write_text((body + "\nfn main() -> Int32 { return 0 }\n") if body.startswith("static ") else
                              ("fn main() -> Int32 {\n" + body + "\nreturn 0\n}\n"))
            for command in ("check", "build"):
                result = run([claw, command, str(source)] + ([str(target)] if command == "build" else []))
                assert result.returncode != 0, f"{name} unexpectedly passed {command}"
                assert "error[semantic]" in result.stdout and diagnostic in result.stdout, f"{name}/{command}:\n{result.stdout}"
                assert "Compilation failed:" not in result.stdout, f"{name} failed too late in lowering"
                assert not target.exists() and not target.with_suffix(".ll").exists(), name
    print(f"[float/pass] {len(cases)} independent rejection cases through check and build")

    clang = shutil.which("clang")
    assert clang, "clang is required for native float regression"
    with tempfile.TemporaryDirectory(prefix="claw-float-sanitizer-") as temp:
        tempdir = Path(temp)
        probe_c = tempdir / "probe.c"
        probe_out = tempdir / "probe.exe"
        probe_c.write_text("int main(void) { return 0; }\n")
        flags = ["-fsanitize=address,undefined,leak", "-fno-omit-frame-pointer"]
        probe = run([clang, *flags, str(probe_c), "-o", str(probe_out)])
        if probe.returncode:
            print("[float/sanitizer/skip] toolchain cannot link ASan/UBSan/LSan probe")
        else:
            for index, (input_ll, expected) in enumerate(((ll, SUCCESS), (paths_ll, ""))):
                sanitized = tempdir / f"float-sanitized-{index}.exe"
                require_success(run([clang, *flags, "-O1", "-g", str(input_ll), str(RUNTIME), "-o", str(sanitized)]), "sanitizer link")
                checked = run([str(sanitized)], env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                                                    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
                require_success(checked, "sanitized native execution")
                assert checked.stdout.replace("\r", "") == expected, checked.stdout
            print("[float/sanitizer/pass] emitted IR and runtime under ASan/UBSan/LSan")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
