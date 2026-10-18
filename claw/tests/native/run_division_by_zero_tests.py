#!/usr/bin/env python3
"""Execute the supported native integer /, %, /= and %= contract end-to-end.

C wrappers supply runtime operands across a separately compiled ABI boundary.
Generated sources, IR, harnesses and executables remain in native/artifacts.
-O0 relinking is an optimizer stress test, not a Cat target debug profile.
"""
from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
ARTIFACTS = HERE / "artifacts" / "division_by_zero"
FIXTURES = HERE / "fixtures"
FATAL = "fatal: integer division or remainder by zero; divisor must be nonzero\n"
WIDTHS = {**{f"Int{n}": n for n in (8, 16, 32, 64, 128)},
          **{f"UInt{n}": n for n in (8, 16, 32, 64, 128)},
          "USize": 64, "Int": 64, "UInt": 64}
COUNTS = {"success": 0, "zero_traps": 0, "rejections": 0}


def fail(message, result=None):
    if result is not None:
        message += f"\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    raise SystemExit("integer division tests: " + message)


def run(command, **kwargs):
    try:
        return subprocess.run([str(x) for x in command], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=120, **kwargs)
    except (OSError, subprocess.TimeoutExpired) as error:
        fail(f"cannot execute {command}: {error}")


def success(command, label, expected="1\n"):
    result = run(command)
    if result.returncode or result.stderr or result.stdout.replace("\r\n", "\n") != expected:
        fail(label, result)
    COUNTS["success"] += 1


def trap(command, label):
    result = run(command)
    if result.returncode == 0 or result.stderr.replace("\r\n", "\n") != FATAL:
        fail(label + ": expected nonzero termination and exact fatal diagnostic", result)
    # A printed marker after the invalid operation would mean execution continued.
    if "continued-after-invalid-operation" in result.stdout:
        fail(label + ": continued after defect", result)
    COUNTS["zero_traps"] += 1


def compiler():
    configured = os.environ.get("CLAW_EXE")
    candidates = [pathlib.Path(configured)] if configured else [
        ROOT / "build" / "claw", ROOT / "build-ucrt64-clang" / "claw.exe",
        ROOT / "build-ucrt64-clang" / "claw-codex.exe"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    fail("set CLAW_EXE to a built compiler")


def source(stem, text):
    path = ARTIFACTS / (stem + ".cat")
    path.write_text(text, encoding="utf-8")
    return path


def emit(claw, stem, text=None, path=None):
    path = path or source(stem, text)
    result = run([claw, "llvm", path])
    if result.returncode:
        fail("LLVM emission rejected " + stem, result)
    output = ARTIFACTS / (stem + ".ll")
    output.write_text(result.stdout, encoding="utf-8")
    return output


def build(claw, stem, text):
    path = source(stem, text)
    output = ARTIFACTS / (stem + ".exe")
    result = run([claw, "build", path, output])
    if result.returncode or not output.is_file() or not output.with_suffix(".ll").is_file():
        fail("native build rejected " + stem, result)
    return output


def link(clang, ir, harness, stem, flags):
    output = ARTIFACTS / (stem + ".exe")
    result = run([clang, "-Wno-override-module", *flags, ir, harness,
                  ROOT / "runtime" / "native_runtime.c", "-o", output])
    if result.returncode:
        fail("clang link failed: " + stem, result)
    return output


def literal(value, typ):
    return f"{value}_{typ}"


def dynamic_module(typ):
    signed = typ.startswith("Int")
    width = WIDTHS[typ]
    numerator = -7 if signed else 2 ** width - 1
    denominator = 3 if signed else 2
    quotient = -2 if signed else numerator // denominator
    remainder = -1 if signed else numerator % denominator
    functions, cases = [], []
    definitions = [f"shape Box {{ value: {typ} }}"]

    def add(name, body, zero_traps=True):
        functions.append(f"share fn {name}(mode: Int32) -> Int32 {{\n{body}\n}}")
        cases.append((name, zero_traps))

    for op, opname, expected in (("/", "quot", quotient), ("%", "rem", remainder)):
        for form in ("binary", "compound", "field"):
            common = (f"    var d: {typ} = if mode == 0 {{ {literal(0, typ)} }} "
                      f"else {{ {literal(denominator, typ)} }}\n")
            if form == "binary":
                body = f"    val result = {literal(numerator, typ)} {op} d\n"
            elif form == "compound":
                body = f"    var result: {typ} = {literal(numerator, typ)}\n    result {op}= d\n"
            else:
                body = f"    var box = Box {{ value: {literal(numerator, typ)} }}\n    box.value {op}= d\n    val result = box.value\n"
            add(opname + "_" + form, common + body +
                f"    if result == {literal(expected, typ)} {{ return 1 }}\n    return 0")

    for op, opname in (("/", "quot"), ("%", "rem")):
        add("zero_numerator_" + opname,
            f"    val d: {typ} = if mode == 0 {{ {literal(1, typ)} }} else {{ {literal(2, typ)} }}\n"
            f"    val result = {literal(0, typ)} {op} d\n"
            f"    if result == {literal(0, typ)} {{ return 1 }}\n    return 0", False)

    if signed:
        minimum = -(2 ** (width - 1))
        for op, opname, expected in (("/", "quot", minimum), ("%", "rem", 0)):
            for form in ("binary", "compound", "field"):
                # Both modes are checked: MIN/-1 and its valid MIN/1 neighbour.
                common = (f"    val d: {typ} = if mode == 0 {{ {literal(-1, typ)} }} "
                          f"else {{ {literal(1, typ)} }}\n")
                if form == "binary":
                    body = f"    val result = {literal(minimum, typ)} {op} d\n"
                elif form == "compound":
                    body = f"    var result: {typ} = {literal(minimum, typ)}\n    result {op}= d\n"
                else:
                    body = f"    var box = Box {{ value: {literal(minimum, typ)} }}\n    box.value {op}= d\n    val result = box.value\n"
                add("min_" + opname + "_" + form, common + body +
                    f"    if result == {literal(expected, typ)} {{ return 1 }}\n    return 0", False)
            neighbour = minimum + 1
            expected_neighbour = -neighbour if op == "/" else 0
            add("min_neighbour_" + opname,
                f"    val d: {typ} = if mode == 0 {{ {literal(-1, typ)} }} else {{ {literal(-1, typ)} }}\n"
                f"    val result = {literal(neighbour, typ)} {op} d\n"
                f"    if result == {literal(expected_neighbour, typ)} {{ return 1 }}\n    return 0", False)
    definitions.extend(functions)
    return "\n".join(definitions) + "\n", cases


def integer_harness(stem, cases):
    declarations = "\n".join(f'extern int32_t f{i}(int32_t) __asm__("{stem}::{name}");'
                             for i, (name, _) in enumerate(cases))
    dispatch = "\n".join(f"    case {i}: result = f{i}(mode); break;" for i in range(len(cases)))
    return f'''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
{declarations}
int main(int argc, char **argv) {{
    if (argc != 3) return 2;
    int which = atoi(argv[1]), mode = atoi(argv[2]), result = 0;
    switch (which) {{
{dispatch}
    default: return 2;
    }}
    printf("%d\\n", result);
    return result == 1 ? 0 : 1;
}}
'''


def assert_integer_ir(path):
    text = path.read_text(encoding="utf-8")
    operations = re.findall(r"^\s+%[^=]+ = (?:sdiv|udiv|srem|urem) .+$", text, re.MULTILINE)
    if not operations or any(not re.search(r", %div\.safe\.divisor\.\d+$", line) for line in operations):
        fail("integer division bypasses the safe divisor lowering: " + path.name)
    if 'declare void @"claw.runtime.defect.division_by_zero"() noreturn' not in text:
        fail("missing noreturn runtime contract: " + path.name)


def execute_cases(executable, cases, label):
    for index, (name, zero_traps) in enumerate(cases):
        success([executable, index, 1], f"{label} {name} nonzero")
        if zero_traps:
            trap([executable, index, 0], f"{label} {name} zero")
        else:
            success([executable, index, 0], f"{label} {name} boundary")


def sanitizer_flags(clang):
    flags = ["-fsanitize=address,undefined,leak", "-fno-omit-frame-pointer", "-O1"]
    with tempfile.TemporaryDirectory(prefix="cat-integer-sanitizer-") as directory:
        probe = pathlib.Path(directory) / "probe.exe"
        result = run([clang, *flags, "-x", "c", "-", "-o", probe], input="int main(void) { return 0; }\n")
        if result.returncode:
            print("[sanitizer/skip] clang ASan/UBSan/LSan runtimes unavailable")
            return None
        success([probe], "sanitizer availability", "")
    return flags


def test_integer_matrix(claw, clang, sanflags):
    function_count = 0
    for typ in WIDTHS:
        stem = "dynamic_" + typ.lower()
        text, cases = dynamic_module(typ)
        ir = emit(claw, stem, text)
        assert_integer_ir(ir)
        harness = ARTIFACTS / (stem + "_harness.c")
        harness.write_text(integer_harness(stem, cases), encoding="utf-8")
        for opt in ("-O0", "-O2", "-O3"):
            executable = link(clang, ir, harness, stem + opt.replace("-", "_"), [opt])
            execute_cases(executable, cases, typ + " " + opt)
        if sanflags:
            executable = link(clang, ir, harness, stem + "_sanitized", sanflags)
            execute_cases(executable, cases, typ + " ASan/UBSan/LSan")
        function_count += len(cases)
    return function_count


def test_constants(claw):
    for typ, width in WIDTHS.items():
        one, zero, seven = (literal(n, typ) for n in (1, 0, 7))
        for op, opname in (("/", "quot"), ("%", "rem")):
            forms = {
                "literal": f"val result = {seven} {op} {zero}",
                "computed": f"val result = {seven} {op} ({one} - {one})",
                "local": f"val d: {typ} = {zero}\n    val result = {seven} {op} d",
                "compound_literal": f"var result: {typ} = {seven}\n    result {op}= {zero}",
            }
            for form, body in forms.items():
                executable = build(claw, f"constant_{typ.lower()}_{opname}_{form}",
                    f'fn main() -> Int32 {{\n    {body}\n    println(result)\n    println("continued-after-invalid-operation")\n    return 0\n}}\n')
                trap([executable], f"{typ} {opname} {form}")
            if typ.startswith("Int"):
                minimum = literal(-(2 ** (width - 1)), typ)
                minus_one = literal(-1, typ)
                expected = minimum if op == "/" else zero
                for form in ("literal", "local", "compound", "field"):
                    prefix = f"shape Box {{ value: {typ} }}\n" if form == "field" else ""
                    if form == "literal":
                        body = f"val result = {minimum} {op} {minus_one}"
                    elif form == "local":
                        body = f"val d: {typ} = {minus_one}\n    val result = {minimum} {op} d"
                    elif form == "compound":
                        body = f"var result: {typ} = {minimum}\n    result {op}= {minus_one}"
                    else:
                        body = f"var box = Box {{ value: {minimum} }}\n    box.value {op}= {minus_one}\n    val result = box.value"
                    executable = build(claw, f"constant_min_{typ.lower()}_{opname}_{form}", prefix +
                        f"fn main() -> Int32 {{\n    {body}\n    if result == {expected} {{ return 0 }}\n    return 1\n}}\n")
                    success([executable], f"{typ} MIN/-1 {opname} {form}", "")
    for op, opname in (("/", "quot"), ("%", "rem")):
        executable = build(claw, "static_zero_" + opname,
            f"static zero: Int32 = 0\nfn main() -> Int32 {{ return 7 {op} zero }}\n")
        trap([executable], "static zero " + opname)
        path = source("rejected_static_" + opname,
            f"static value: Int32 = 7 {op} 0\nfn main() -> Int32 {{ return value }}\n")
        result = run([claw, "check", path])
        if result.returncode == 0 or "Static variable initializer must be a compile-time literal expression." not in result.stderr + result.stdout:
            fail("static constant-expression boundary changed", result)
        COUNTS["rejections"] += 1
    for op in ("/=", "%="):
        path = source("rejected_array_" + ("quot" if op == "/=" else "rem"),
            f"fn main() -> Int32 {{ var values: Array[Int32, 1] = [7]\n values[0] {op} 2\n return 0 }}\n")
        result = run([claw, "check", path])
        if result.returncode == 0 or "compound" not in (result.stderr + result.stdout).lower():
            fail("unsupported fixed-array compound assignment boundary changed", result)
        COUNTS["rejections"] += 1


def test_float(claw, clang, sanflags):
    for typ, ctype in (("Float32", "float"), ("Float64", "double"), ("Float", "double")):
        stem = "floating_" + typ.lower()
        definitions = []
        for name, numerator, compound in (("positive", "1.0", False), ("negative", "-1.0", False),
                                           ("nan_value", "0.0", False), ("compound", "1.0", True),
                                           ("negative_zero", "1.0", False)):
            denom = "-0.0" if name == "negative_zero" else "0.0"
            body = f"var x: {typ} = {numerator}_{typ}\n    val d: {typ} = if mode == 0 {{ {denom}_{typ} }} else {{ 2.0_{typ} }}\n"
            body += "    x /= d\n    return x" if compound else "    return x / d"
            definitions.append(f"share fn {name}(mode: Int32) -> {typ} {{\n    {body}\n}}")
        ir = emit(claw, stem, "\n".join(definitions) + "\n")
        text = ir.read_text(encoding="utf-8")
        if "fdiv" not in text or "claw.runtime.defect.division_by_zero" in text:
            fail("floating division acquired an integer guard")
        harness = ARTIFACTS / (stem + "_harness.c")
        declarations = "\n".join(f'extern {ctype} {name}(int32_t) __asm__("{stem}::{name}");'
                                  for name in ("positive", "negative", "nan_value", "compound", "negative_zero"))
        harness.write_text(f'''#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
{declarations}
int main(int argc, char **argv) {{
    if (argc != 2) return 2;
    int mode = atoi(argv[1]);
    {ctype} p=positive(mode), n=negative(mode), z=nan_value(mode), c=compound(mode), s=negative_zero(mode);
    int ok = mode == 0 ? (isinf(p) && !signbit(p) && isinf(n) && signbit(n) && isnan(z) && isinf(c) && !signbit(c) && isinf(s) && signbit(s))
                       : (p == 0.5 && n == -0.5 && z == 0.0 && c == 0.5 && s == 0.5);
    printf("%d\\n", ok ? 1 : 0); return ok ? 0 : 1;
}}
''', encoding="utf-8")
        flags = [(opt.replace("-", "_"), [opt]) for opt in ("-O0", "-O2", "-O3")]
        if sanflags:
            flags.append(("_sanitized", sanflags))
        for label, options in flags:
            executable = link(clang, ir, harness, stem + label, options)
            for mode in (0, 1):
                success([executable, mode], typ + " IEEE754/finite " + label)


def test_expression_paths(claw, clang, sanflags):
    stem = "revise_integer_division_paths"
    path = FIXTURES / (stem + ".cat")
    harness = FIXTURES / (stem + "_harness.c")
    if not path.is_file() or not harness.is_file():
        fail("missing supported-expression fixtures")
    ir = emit(claw, stem, path=path)
    assert_integer_ir(ir)
    # The C harness's list mode is the source of case metadata, avoiding a second
    # handwritten dispatch table. Each row is: index, zero-mode action, safe stdout.
    executable = link(clang, ir, harness, stem + "_O2", ["-O2"])
    listing = run([executable, "--list"])
    if listing.returncode:
        fail("expression harness metadata listing failed", listing)
    rows = []
    for row in listing.stdout.splitlines():
        index, action = row.split()
        if action not in ("trap", "safe"):
            fail("invalid expression case metadata")
        rows.append((index, action))
    if not rows:
        fail("no expression cases executed")
    variants = [("_O2", executable)]
    for opt in ("-O0", "-O3"):
        variants.append((opt, link(clang, ir, harness, stem + opt.replace("-", "_"), [opt])))
    if sanflags:
        variants.append(("sanitizer", link(clang, ir, harness, stem + "_sanitized", sanflags)))
    for label, executable in variants:
        for index, action in rows:
            success([executable, index, 1], "expression " + index + " " + label)
            if action == "trap":
                trap([executable, index, 0], "expression zero " + index + " " + label)
            else:
                success([executable, index, 0], "expression skipped/wrapped " + index + " " + label)
    return len(rows)


def main():
    # Intentional abort cases should not litter workspaces with core dumps.
    try:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    except (ImportError, ValueError, OSError):
        pass
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    claw = compiler()
    clang = shutil.which("clang")
    if not clang:
        fail("clang is required by the native test suite")
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    sanflags = sanitizer_flags(clang)
    functions = test_integer_matrix(claw, clang, sanflags)
    test_constants(claw)
    test_float(claw, clang, sanflags)
    expressions = test_expression_paths(claw, clang, sanflags)
    print(f"integer division/remainder tests passed: {len(WIDTHS)} integer types/aliases, "
          f"{functions} dynamic functions, {expressions} expression cases, "
          f"{COUNTS['success']} successful runs, {COUNTS['zero_traps']} exact zero traps, "
          f"{COUNTS['rejections']} preserved syntax-boundary rejections; "
          "all dynamic cases executed at O0/O2/O3" + (" and ASan/UBSan/LSan" if sanflags else "") +
          f"; artifacts: {ARTIFACTS}")


if __name__ == "__main__":
    main()
