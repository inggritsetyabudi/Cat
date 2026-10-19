#!/usr/bin/env python3
"""End-to-end PoC gates; keep generated Cat, LLVM IR, objects and executables."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import shutil


def verify_target(obj, ir=None):
    data = obj.read_bytes()
    if sys.platform == "win32":
        assert len(data) >= 20 and struct.unpack_from("<H", data, 0)[0] == 0x8664, "not an AMD64 COFF object"
        sections, optional_size = struct.unpack_from("<HH", data, 2)[0], struct.unpack_from("<H", data, 16)[0]
        assert sections > 0 and optional_size == 0, "not a relocatable COFF object"
        evidence = {"object_format": "COFF", "object_machine": "AMD64 (0x8664)",
                    "coff_sections": sections, "coff_optional_header_size": optional_size,
                    "object_bytes": len(data)}
    else:
        assert data[:6] == b"\x7fELF\x02\x01", "not a little-endian ELF64 object"
        assert struct.unpack_from("<HH", data, 16) == (1, 62), "not an x86_64 relocatable object"
        evidence = {"object_format": "ELF64", "object_machine": "x86_64", "object_bytes": len(data)}
    if ir is not None:
        text = ir.read_text()
        triple = re.search(r'^target triple = "([^"]+)"$', text, re.MULTILINE)
        layout = re.search(r'^target datalayout = "([^"]+)"$', text, re.MULTILINE)
        expected_triple = "x86_64-w64-windows-gnu" if sys.platform == "win32" else "x86_64-unknown-linux-gnu"
        layout_prefix = "e-m:w-" if sys.platform == "win32" else "e-m:e-"
        assert triple and triple.group(1) == expected_triple, "unexpected native IR triple"
        assert layout and layout.group(1).startswith(layout_prefix) and "n8:16:32:64" in layout.group(1), "unexpected native DataLayout"
        evidence.update(target_triple=triple.group(1), data_layout=layout.group(1))
    return evidence


def run(args, *, expected=0, env=None):
    result = subprocess.run([str(arg) for arg in args], capture_output=True, text=True, env=env)
    if result.returncode != expected:
        raise AssertionError(f"{args}: exit {result.returncode}, expected {expected}\n{result.stdout}\n{result.stderr}")
    return result


def executable(path):
    """Require the exact fresh output, including an explicit .exe on Windows."""
    assert path.is_file(), f"missing native executable {path}"
    if sys.platform == "win32":
        data = path.read_bytes()
        assert path.suffix.lower() == ".exe" and data[:2] == b"MZ", "not a native Windows executable"
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        assert data[pe:pe + 4] == b"PE\x00\x00" and struct.unpack_from("<H", data, pe + 4)[0] == 0x8664, "not an AMD64 PE executable"
        assert struct.unpack_from("<H", data, pe + 24)[0] == 0x20B, "not a PE32+ executable"
    return path


def imported_dlls(readobj, binary):
    result = run([readobj, "--coff-imports", binary])
    # llvm-readobj has used both `DLL Name:` and `Name:` labels across releases.
    names = set(re.findall(r"(?:DLL\s+)?Name:\s*([^\r\n]+\.dll)\s*$", result.stdout, re.I | re.M))
    return {name.strip().strip('"') for name in names}


def isolated_windows_emitter(root, poc, readobj):
    """Copy only the emitter and its non-system DLL closure into an empty PATH."""
    assert sys.platform == "win32"
    poc = executable(poc).resolve()
    isolation = root / "windows-emitter-isolation"
    isolation.mkdir(exist_ok=True)
    for prior in isolation.iterdir():
        assert prior.is_file() and (prior.suffix.lower() == ".dll" or prior.name == poc.name), "unexpected file in emitter isolation directory"
        prior.unlink()
    copied = set()
    queue = [poc.name]
    search_dirs = [poc.parent] + [Path(p) for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    system_names = ("api-ms-win", "ext-ms-win")
    system_directory = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"

    def is_system_dll(name):
        return name.lower().startswith(system_names) or (system_directory / name).is_file()

    while queue:
        name = queue.pop(0)
        assert Path(name).name == name, "DLL import must be a basename"
        source = poc if Path(name).name.lower() == poc.name.lower() else next(
            (directory / name for directory in search_dirs if (directory / name).is_file()), None)
        assert source and source.is_file(), f"cannot locate emitter dependency {name}"
        target = isolation / Path(name).name
        if target.resolve() != source.resolve():
            shutil.copy2(source, target)
        if target.name.lower() in copied:
            continue
        copied.add(target.name.lower())
        for dep in imported_dlls(readobj, target):
            assert not dep.lower().startswith(("llvm", "libllvm")), "static-component emitter unexpectedly imports an LLVM DLL"
            if is_system_dll(dep):
                continue
            queue.append(dep)
    imports = imported_dlls(readobj, isolation / poc.name)
    assert imports, "missing COFF import evidence for emitter"
    assert all((isolation / dep).is_file() or is_system_dll(dep) for dep in imports)
    assert all(path.name == poc.name or path.suffix.lower() == ".dll" for path in isolation.iterdir()), "external tool copied into isolation directory"
    env = os.environ.copy()
    env["PATH"] = str(isolation)
    return isolation, env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--poc", type=Path, required=True)
    parser.add_argument("--claw", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--readobj", default=None, help="llvm-readobj used for Windows DLL/import evidence")
    args = parser.parse_args()
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}:
        parser.error("PoC test suite requires native x86_64 Linux or native Windows.")
    if sys.platform == "win32":
        assert os.name == "nt", "Windows PoC proof must run under native Windows Python"
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    successes = {
        "constant": ("fn main() -> Int32 { return 42 }", 42),
        "unit": ("fn main() {}", 0),
        "call": ("fn answer(x: Int32) -> Int32 { return x }\nfn main() -> Int32 { return answer(37) }", 37),
        "branch_phi": ("fn choose(x: Int32) -> Int32 { val y = if x < 4 { 17 } else { 29 }; return y }\nfn main() -> Int32 { return choose(3) }", 17),
        "branch_false": ("fn choose(x: Int32) -> Int32 { val y = if x >= 4 { 29 } else { 17 }; return y }\nfn main() -> Int32 { return choose(4) }", 29),
        "mutable": ("fn main() -> Int32 { var x: Int32; x = 5; x = 31; return x }", 31),
        "shadow": ("fn main() -> Int32 { val x: Int32 = 9; if true { val x: Int32 = 21; if x == 0 { return 1 } }; return x }", 9),
        "bool_call": ("fn select(flag: Bool) -> Int32 { if flag { return 23 } else { return 7 } }\nfn main() -> Int32 { return select(false) }", 7),
        "bool_not": ("fn select(flag: Bool) -> Int32 { if not flag { return 23 } else { return 7 } }\nfn main() -> Int32 { return select(false) }", 23),
        "unit_call": ("fn noop() {}\nfn main() -> Int32 { noop(); return 11 }", 11),
        "compare_neq": ("fn select(x: Int32) -> Int32 { if x != 4 { return 23 } else { return 7 } }\nfn main() -> Int32 { return select(4) }", 7),
        "compare_lte": ("fn select(x: Int32) -> Int32 { if x <= 4 { return 23 } else { return 7 } }\nfn main() -> Int32 { return select(4) }", 23),
        "compare_gt": ("fn select(x: Int32) -> Int32 { if x > 4 { return 23 } else { return 7 } }\nfn main() -> Int32 { return select(3) }", 7),
        "bool_equal": ("fn equal(x: Bool) -> Bool { return x == false }\nfn main() -> Int32 { if equal(false) { return 19 } else { return 5 } }", 19),
        "param_shadow": ("fn select(x: Int32) -> Int32 { if x < 9 { val x: Int32 = 88; if x == 0 { return 1 } }; return x }\nfn main() -> Int32 { return select(4) }", 4),
    }
    negatives = {
        "signed_add": "fn main() -> Int32 { val x: Int32 = 1; return x + 2 }",
        "division": "fn main() -> Int32 { val x: Int32 = 4; return x / 0 }",
        "runtime_call": "fn main() { println(1) }",
        "owned": 'fn main() { val s = String.from("owned"); println(s.as_str()) }',
        "float": "fn f(x: Float64) -> Float64 { return x }\nfn main() -> Int32 { val y = f(1.5); return 0 }",
        "borrow_abi": "fn read(x: ref Int32) -> Int32 { return 4 }\nfn main() -> Int32 { val x: Int32 = 4; return read(ref x) }",
        "uint32": "fn check(x: UInt32) -> Bool { return x > 2_UInt32 }\nfn main() -> Int32 { return 0 }",
        "shape": "shape Point { x: Int32 }\nfn main() -> Int32 { val p = Point { x: 3 }; return p.x }",
        "static": "static answer: Int32 = 4\nfn main() -> Int32 { return answer }",
        "entry_call": "fn main() -> Int32 { return main() }",
        "entry_via_helper": "fn helper() -> Int32 { return main() }\nfn main() -> Int32 { return helper() }",
        "entry_params": "fn main(x: Int32) -> Int32 { return x }",
        "no_entry": "fn answer() -> Int32 { return 1 }",
        "uninitialized": "fn main() -> Int32 { var x: Int32; return x }",
        "type_error": "fn main() -> Int32 { return true }",
        "ownership_error": 'fn main() { val x = String.from("x"); val y = x; println(x.as_str()); println(y.as_str()) }',
    }
    results = []
    for name, (source, expected) in successes.items():
        case = root / name
        case.mkdir(exist_ok=True)
        src, obj, ir, exe, baseline = [case / filename for filename in (f"{name}.cat", "program.o", "program.ll", "program.exe" if sys.platform == "win32" else "program", "baseline.exe")]
        for output in (obj, ir, exe, baseline, baseline.with_suffix(".ll")):
            output.unlink(missing_ok=True)
        src.write_text(source.replace(";", "\n") + "\n")
        result = run([args.poc, src, "-o", obj, "--emit-ir", ir])
        (case / "poc.log").write_text(result.stdout + result.stderr)
        target_evidence = verify_target(obj, ir)
        text = ir.read_text()
        assert "target datalayout" in text and "target triple" in text
        assert "define i32 @main()" in text, f"{name}: no native entry wrapper"
        if name == "branch_phi":
            assert "br i1" in text and "if_join" in text, "if-expression did not exercise control flow"
        # This is the only link step: no .cat/.ll/runtime C is passed to cc.
        link_argv = [args.cc, obj, "-o", exe]
        run(link_argv)
        native_exe = executable(exe)
        native = run([native_exe], expected=expected)
        original = run([args.claw, "build", src, baseline])
        (case / "baseline-build.log").write_text(original.stdout + original.stderr)
        old = run([executable(baseline)], expected=expected)
        assert native.stdout == old.stdout and native.stderr == old.stderr
        assert native.stdout == "" and native.stderr == "", f"{name}: unexpected native output"
        results.append({"name": name, "status": "pass", "exit": expected, "kind": "differential",
                        "link_argv": [str(x) for x in link_argv],
                        "run_argv": [str(native_exe)],
                        "run": {"exit": native.returncode, "stdout": native.stdout, "stderr": native.stderr},
                        **target_evidence})
        print(f"[pass/differential] {name}", flush=True)
    for name, source in negatives.items():
        case = root / name
        case.mkdir(exist_ok=True)
        src, obj, ir = case / f"{name}.cat", case / "rejected.o", case / "rejected.ll"
        src.write_text(source.replace(";", "\n") + "\n")
        obj.unlink(missing_ok=True)
        ir.unlink(missing_ok=True)
        result = run([args.poc, src, "-o", obj, "--emit-ir", ir], expected=1)
        (case / "rejection.log").write_text(result.stdout + result.stderr)
        assert result.stderr.strip(), f"{name}: missing diagnostic"
        assert not obj.exists(), f"{name}: rejected program emitted object"
        assert not ir.exists(), f"{name}: rejected program emitted IR"
        if name not in {"entry_call", "entry_via_helper", "entry_params", "no_entry", "uninitialized", "type_error", "ownership_error"}:
            # Distinguish deliberate backend rejection from broken fixture syntax.
            run([args.claw, "check", src])
            assert "llvm-cpp-poc" in result.stderr, result.stderr
            assert any(word in result.stderr.lower() for word in ("unsupported", "rejects")), result.stderr
        elif name in {"entry_call", "entry_via_helper"}:
            assert "main" in result.stderr, result.stderr
        elif name == "uninitialized":
            assert "error[ownership]" in result.stderr and "uninitialized" in result.stderr, result.stderr
        elif name == "ownership_error":
            assert "error[ownership]" in result.stderr and "moved value" in result.stderr, result.stderr
        elif name == "type_error":
            assert "error[semantic]" in result.stderr and "mismatch" in result.stderr, result.stderr
        else:
            assert "Entry must define fn main()" in result.stderr, result.stderr
        results.append({"name": name, "status": "pass", "kind": "rejection"})
        print(f"[pass/rejection] {name}", flush=True)
    # Paths containing spaces and shell metacharacters must stay ordinary paths.
    case = root / "space ; literal"
    case.mkdir(exist_ok=True)
    src, obj, ir, exe = [case / name for name in ("hello ; x.cat", "object ; x.o", "ir ; x.ll", "program ; x.exe" if sys.platform == "win32" else "program ; x")]
    src.write_text("fn main() -> Int32 { return 13 }\n")
    for output in (obj, ir, exe):
        output.unlink(missing_ok=True)
    run([args.poc, src, "-o", obj, "--emit-ir", ir])
    link_argv = [args.cc, obj, "-o", exe]
    run(link_argv)
    literal_run = run([executable(exe)], expected=13)
    results.append({"name": "literal_paths", "status": "pass", "kind": "io",
                    "link_argv": [str(x) for x in link_argv],
                    "run": {"exit": literal_run.returncode, "stdout": literal_run.stdout, "stderr": literal_run.stderr}})
    # Failure to create an output must be a diagnostic, not a successful compile.
    source = root / "constant" / "constant.cat"
    bad = run([args.poc, source, "-o", root / "missing-parent" / "program.o"], expected=1)
    assert bad.stderr.strip()
    aliased = run([args.poc, source, "-o", source], expected=1)
    assert "distinct" in aliased.stderr
    old_ir = root / "preserve-ir.ll"
    old_ir.write_bytes(b"prior IR artifact\n")
    rejected = run([args.poc, source, "-o", root / "missing-parent" / "program.o", "--emit-ir", old_ir], expected=1)
    assert old_ir.read_bytes() == b"prior IR artifact\n" and rejected.stderr.strip()
    old_object = root / "preserve-object.o"
    old_object.write_bytes(b"prior object artifact\n")
    rejected = run([args.poc, source, "-o", old_object, "--emit-ir", root / "missing-parent" / "program.ll"], expected=1)
    assert old_object.read_bytes() == b"prior object artifact\n" and rejected.stderr.strip()
    blocked = root / "output-is-directory"
    blocked.mkdir(exist_ok=True)
    rejected = run([args.poc, source, "-o", old_object, "--emit-ir", blocked], expected=1)
    assert old_object.read_bytes() == b"prior object artifact\n" and rejected.stderr.strip()
    assert not list(root.glob("*.tmp")), "failed output staging leaked temporary files"
    results.extend([{"name": "unwritable_output", "status": "pass", "kind": "io"},
                    {"name": "source_alias", "status": "pass", "kind": "io"},
                    {"name": "preserve_ir_on_object_failure", "status": "pass", "kind": "io"},
                    {"name": "preserve_object_on_ir_failure", "status": "pass", "kind": "io"},
                    {"name": "nonregular_ir_output", "status": "pass", "kind": "io"}])
    before = source.read_bytes()
    for name, output, irpath in (
        ("ir_source_alias", root / "alias.o", source),
        ("ir_object_alias", root / "same.o", root / "same.o"),
    ):
        output.unlink(missing_ok=True)
        rejected = run([args.poc, source, "-o", output, "--emit-ir", irpath], expected=1)
        assert "distinct" in rejected.stderr and not output.exists()
        results.append({"name": name, "status": "pass", "kind": "io"})
    hardlink = root / "source-hardlink.o"
    hardlink.unlink(missing_ok=True)
    os.link(source, hardlink)
    rejected = run([args.poc, source, "-o", hardlink], expected=1)
    assert "distinct" in rejected.stderr and source.read_bytes() == before
    hardlink.unlink()
    results.append({"name": "hardlink_source_alias", "status": "pass", "kind": "io"})
    # Object generation must work even when no external compiler/linker is on PATH.
    env = os.environ.copy()
    empty_path = root / "empty-tool-path"
    empty_path.mkdir(exist_ok=True)
    env["PATH"] = str(empty_path)
    obj = root / "no-toolchain.o"
    obj.unlink(missing_ok=True)
    if sys.platform == "win32":
        readobj = args.readobj or shutil.which("llvm-readobj")
        assert readobj, "Windows isolation requires llvm-readobj (use --readobj)"
        isolated, env = isolated_windows_emitter(root, args.poc, Path(readobj))
        # Deliberately invoke only the copied emitter with PATH containing no compiler/linker.
        isolated_poc = isolated / executable(args.poc).name
        run([isolated_poc, source, "-o", obj], env=env)
        isolation_evidence = {"isolated_path": str(isolated), "isolated_path_entries": [str(isolated)],
                              "isolated_files": sorted(p.name for p in isolated.iterdir())}
    else:
        run([args.poc.resolve(), source, "-o", obj], env=env)
        isolation_evidence = {"isolated_path_entries": [str(empty_path)]}
    verify_target(obj)
    results.append({"name": "no_toolchain_path", "status": "pass", "kind": "isolation", **isolation_evidence})
    (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"PASS: {len(results)} PoC cases; artifacts: {root}")


if __name__ == "__main__":
    main()
