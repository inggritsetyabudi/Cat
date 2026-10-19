#!/usr/bin/env python3
"""Bounded String E2E gate: real Cat frontend, LLVM API, isolated LLD.

All production comparisons and C instrumentation preparation finish before the
compiler-free phase. Sanitized runtime contains no observer pointer registry.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import sys
import time
if sys.platform == "linux":
    import resource
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lld.test_lld import profile, link_argv, verify_exe, isolate, inspect, command, HarnessError, file_record, fresh, sha256
from test_poc import verify_target
from string_support import prepare, stamp, run, clean_ledger, sanitizer_argv, SAN_ENV

REPO = Path(__file__).resolve().parents[4]
ORACLE = REPO / "claw/experimental/llvm_cpp/abi_oracle"
SAFE_ORACLE = ("abi_calls", "literal_str", "owned_scope", "owned_borrow", "owned_helper", "owned_branch", "owned_empty_clone")
# Fixed expected outputs/counts, independent from either compiler's observed run.
EXTRA = {
    "literal_bytes": ('fn main() { println("") println("é\\0🙂") }', "\né\0🙂\n".encode(), 0),
    "shadow_owner": ('fn main() { val owner = String.from("outer") if true { val owner = String.from("i") println(owner.as_str()) } println(owner.as_str()) }', b"i\nouter\n", 2),
    "reverse_scope": ('fn main() { val first = String.from("a") val second = String.from("bb") val third = String.from("ccc") println(third.as_str()) }', b"ccc\n", 3),
    "alias_views": ('fn main() { val owner = String.from("borrow") val first = owner.as_str() val second = owner.as_str() println(first) println(second) }', b"borrow\nborrow\n", 1),
    "shadow_parameter": ('fn show(value: String) { if true { val value = String.from("i") println(value.as_str()) } println(value.as_str()) }\nfn main() { show(String.from("outer")) }', b"i\nouter\n", 2),
    "early_return": ('fn show(early: Bool) { val owner = String.from("kept") if early { println(owner.as_str()) return } println(owner.as_str()) }\nfn main() { show(true) show(false) }', b"kept\nkept\n", 2),
    "branch_distinct": ('fn show(left: Bool) { if left { val left_owner = String.from("left") println(left_owner.as_str()) } else { val right_owner = String.from("right") println(right_owner.as_str()) } }\nfn main() { show(true) show(false) }', b"left\nright\n", 2),
    "loop_owner": ('fn main() { var i: Int32\ni = 0\nloop i < 3 { val owner = String.from("x") println(owner.as_str()) i = i + 1 } }', b"x\nx\nx\n", 3),
    "loop_exit": ('fn main() { var i: Int32\ni = 0\nloop i < 4 { val owner = String.from("x") i = i + 1\nif i == 1 { skip }\nprintln(owner.as_str())\nif i == 3 { stop } } }', b"x\nx\n", 3),
    "temporary": ('fn main() { println(String.from("tmp").as_str()) println(String.from("a").clone().as_str()) }', b"tmp\na\n", 3),
    "clone_independence": ('fn make() -> String { val owner = String.from("copy") return owner.clone() }\nfn main() { val copied = make() println(copied.as_str()) }', b"copy\n", 2),
    "constructor_return": ('fn copy_text(text: Str, n: Int32) -> String { println(n) return String.from(text) }\nfn main() { val empty = copy_text("", 7) val full = copy_text("é\\0🙂", 9) println(empty.as_str()) println(full.as_str()) }', "7\n9\n\né\0🙂\n".encode(), 2),
}


def native_bytes(data):
    return data.replace(b"\n", b"\r\n") if sys.platform == "win32" else data


def no_compiler(env, label):
    for name in ("clang", "clang++", "cc", "gcc", "g++", "cl", "clang-23", "gcc-13"):
        if shutil.which(name, path=env["PATH"]):
            raise HarnessError(f"{label}: compiler reachable: {name}")
    env.update(CC="/__missing_cat_cc__", CXX="/__missing_cat_cc__", CLAW_NATIVE_CLANG="/__missing_cat_cc__", NoDefaultCurrentDirectoryInExePath="1")


def runtime_argv(lld, obj, exe, prof, directory, archive):
    directory.mkdir(parents=True, exist_ok=True)
    argv = link_argv(lld, obj, exe, prof, directory / "link.map")
    index = argv.index("--start-group")
    argv[index:index] = ([archive] if archive else []) + ["--gc-sections"]
    if any(str(x).lower().endswith((".cat", ".ll", ".c", ".cpp")) for x in argv):
        raise HarnessError("source/IR passed to final linker")
    return argv


def check_run(record, expected, code=0, stderr=b""):
    if (record["exit"], record["stdout_hex"], record["stderr_hex"]) != (code, native_bytes(expected).hex(), stderr.hex()):
        raise HarnessError(f"native parity mismatch: {record}")


def audit_ir(text, case):
    if re.search(r"\b(?:sret|byval|nonnull|dereferenceable|nsw|nuw|poison|undef)\b", text):
        raise HarnessError(f"{case}: unproven attribute/overflow flag/value")
    if '%claw.slice' in text and '%claw.slice = type { ptr, i64 }' not in text:
        raise HarnessError(f"{case}: exact Str layout absent")
    if 'claw.runtime.println.slice' not in text:
        raise HarnessError(f"{case}: literal output does not use canonical runtime")
    if case == "abi_calls":
        if '%claw.slice = type { ptr, i64 }' not in text or '%claw.buffer = type { ptr, i64, i64 }' not in text:
            raise HarnessError("exact String layout absent")
        str_arg = "ptr" if sys.platform == "win32" else "%claw.slice"
        contracts = [
            rf'define internal %claw\.slice @"abi_calls::borrowed_ref"\({re.escape(str_arg)} %[^,)]+\)',
            r'define internal void @"abi_calls::owned_helper"\(ptr %[^,)]+, ptr %[^,)]+\)',
            r'define internal void @"abi_calls::make_owned"\(ptr %[^,)]+\)',
            r'declare ptr @"?claw\.runtime\.string\.alloc"?\(i64\)',
            r'declare void @"?claw\.runtime\.string\.copy"?\(ptr, ptr, i64\)',
            r'declare void @"?claw\.runtime\.string\.free"?\(ptr\)',
            r'declare void @"?claw\.runtime\.println\.slice"?\(ptr\)',
        ]
        for contract in contracts:
            if not re.search(contract, text):
                raise HarnessError(f"canonical Cat ABI mismatch: {contract}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("emitter", "production", "runtime", "external", "artifacts", "manifest", "cc", "ar"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--readobj", type=Path)
    parser.add_argument("--ucrt-prefix", type=Path)
    parser.add_argument("--sysroot", type=Path)
    args = parser.parse_args()
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}:
        raise HarnessError("native Linux x86_64 or Windows UCRT x64 only")
    if sys.platform == "linux":
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    root = args.artifacts.absolute(); root.mkdir(parents=True, exist_ok=True)
    report = {"schema": "cat-string-gate/v2", "status": "running", "platform": sys.platform,
              "profile": args.profile, "cases": [], "rejections": [], "negative_links": [], "commands": []}
    def execute(argv, label, env=None, expected=0):
        record = {"phase": label, **run(argv, env=env)}
        report["commands"].append(record)
        if expected is not None and record["exit"] != expected:
            raise HarnessError(f"{label}: unexpected exit {record['exit']}: {record['argv']}\n{record['stderr']}")
        return record
    try:
        manifest = json.loads(args.manifest.read_text())
        package = {role: Path(manifest[role]["path"]).resolve() for role in ("source", "object", "archive")}
        if package["archive"] != args.runtime.resolve():
            raise HarnessError("runtime manifest/archive mismatch")
        for role, path in package.items():
            if sha256(path) != manifest[role]["sha256"]:
                raise HarnessError("runtime package hash mismatch: " + role)
        protected = {str(p): stamp(p) for p in (*package.values(), args.manifest.absolute())}
        report["package"] = manifest
        legacy_lock = json.loads((Path(__file__).parent / "string-legacy-contracts.json").read_text())
        for path, expected_hash in legacy_lock["files"].items():
            if sha256(REPO / path) != expected_hash:
                raise HarnessError("legacy corpus/contract changed: " + path)
        report["legacy_contracts"] = legacy_lock
        expectations = json.loads((ORACLE / "expectations.json").read_text())
        all_cases = {item["name"]: item for item in expectations["cases"]}
        if len(all_cases) != 14 or sum(x["allocations"] for x in all_cases.values()) != 63:
            raise HarnessError("legacy 14-case/63-allocation oracle was altered")
        prof = profile(args)
        report["crt_inputs"] = prof["required_files"]
        prep = prepare(args.cc, args.ar, package["source"], root / "preparation", args.external, prof)
        report["preparation"] = prep
        protected.update(prep["protected_files"])
        prepared = []
        for name in (*SAFE_ORACLE, *EXTRA):
            directory = root / name; directory.mkdir(exist_ok=True)
            cat = directory / (name + ".cat")
            if name in all_cases:
                item = all_cases[name]
                source = (REPO / item["source"]).read_text()
                expected, allocations = bytes.fromhex(item["stdout_hex"]), item["allocations"]
            else:
                source, expected, allocations = EXTRA[name]
            cat.write_text(source + "\n", encoding="utf-8")
            references = {}
            for label, flags in (("O0", []), ("O2", ["--release"])):
                exe = directory / (label + ".production.exe"); fresh(exe)
                built = execute([args.production, "build", *flags, cat, exe], f"reference:{name}:{label}:build")
                observed = execute([exe], f"reference:{name}:{label}:run")
                check_run(observed, expected)
                references[label] = {"build": built, "run": observed, "executable": file_record(exe)}
            # Preserve semantic/ownership/ABI traces from the actual production path.
            for stage in ("oir", "lir"):
                trace = execute([args.production, stage, cat], f"trace:{name}:{stage}")
                (directory / (stage + ".txt")).write_bytes(bytes.fromhex(trace["stdout_hex"]))
            prepared.append((name, directory, cat, expected, allocations, references))
        # Valid production inputs rejected by the bounded adapter, not parser errors.
        reject_paths = {
            "result-try": "claw/tests/integration/fixtures/try_cleanup_minimal.cat",
            "pick-result-loops": "claw/tests/integration/fixtures/composition_owned_result_loops.cat",
            "utf8-range-view": "claw/tests/native/fixtures/revise_string_owner.cat",
            "arena": "claw/tests/native/fixtures/revise_arena.cat",
        }
        reject_prepared = []
        for name, source in reject_paths.items():
            directory = root / ("reject-" + name); directory.mkdir(exist_ok=True)
            cat = directory / (name.replace("-", "_") + ".cat")
            cat.write_bytes((REPO / source).read_bytes())
            reference = directory / "reference.exe"
            execute([args.production, "build", cat, reference], "reject-reference:" + name)
            reject_prepared.append((name, directory, cat))
        # Current LIR omits unique keys for two same-named sibling locals. Do not
        # invent provenance or relax duplicate-binding checks to accept them.
        name = "sibling-owner-alias"
        directory = root / ("reject-" + name); directory.mkdir(exist_ok=True)
        cat = directory / "sibling_owner_alias.cat"
        cat.write_text('fn show(left: Bool) { if left { val owner = String.from("left") println(owner.as_str()) } else { val owner = String.from("right") println(owner.as_str()) } }\nfn main() { show(true) show(false) }\n')
        execute([args.production, "build", cat, directory / "reference.exe"], "reject-reference:" + name)
        for stage in ("oir", "lir"):
            trace = execute([args.production, stage, cat], "reject-trace:" + stage)
            (directory / (stage + ".txt")).write_bytes(bytes.fromhex(trace["stdout_hex"]))
        reject_prepared.append((name, directory, cat))
        ownership_sources = {
            "ownership-use-after-move": 'fn main() { val first = String.from("x") val second = first println(first.as_str()) println(second.as_str()) }',
            "ownership-temporary-escape": 'fn main() { val view = String.from("tmp").as_str() println(view) }',
            "ownership-return-local-view": 'fn bad() -> Str { val owner = String.from("x") return owner.as_str() }\nfn main() { println(bad()) }',
        }
        report["ownership_references"] = []
        for name, source in ownership_sources.items():
            directory = root / ("reject-" + name); directory.mkdir(exist_ok=True)
            cat = directory / (name.replace("-", "_") + ".cat"); cat.write_text(source + "\n")
            for flags in ([], ["--release"]):
                failure = execute([args.production, "build", *flags, cat, directory / "reference.exe"], "ownership-reference:" + name, expected=None)
                if failure["exit"] == 0 or not failure["stderr_hex"]:
                    raise HarnessError("production accepted conflicting ownership: " + name)
                report["ownership_references"].append(failure)
            reject_prepared.append((name, directory, cat))
        inspector = args.readobj if sys.platform == "win32" else Path(shutil.which("readelf") or "/missing-readelf")
        lld, link_env, link_isolation = isolate(args.external.absolute(), "external", root / "linker-isolation", inspector)
        emitter, emit_env, emit_isolation = isolate(args.emitter.absolute(), "emitter", root / "emitter-isolation", inspector)
        no_compiler(link_env, "link"); no_compiler(emit_env, "emit")
        version = command([lld, "--version"], link_env, 0).stdout
        if not re.search(r"^LLD 23\.1\.3(?:\s|\()", version):
            raise HarnessError("LLD must be exact 23.1.3")
        report.update(lld_version=version.strip(), link_isolation=link_isolation, emit_isolation=emit_isolation)
        # No C compilation beyond this point, including sanitizer and OOM variants.
        for name, directory, cat, expected, allocations, refs in prepared:
            obj, ir = directory / "program.o", directory / "program.ll"
            fresh(obj); fresh(ir)
            emitted = execute([emitter, cat, "-o", obj, "--emit-ir", ir, "--strings", "--runtime-i32", "--arithmetic-i32"], "emit:" + name, emit_env)
            target = verify_target(obj, ir)
            audit_ir(ir.read_text(), name)
            row = {"name": name, "source": file_record(cat), "expected_stdout_hex": expected.hex(), "allocations": allocations,
                   "references": refs, "emit": emitted, "target": target, "object": file_record(obj), "ir": file_record(ir), "variants": {}}
            report["cases"].append(row)
            variants = {"native": args.runtime, "observer": Path(prep["observer_archive"]["path"])}
            if sys.platform == "linux":
                variants["sanitized_runtime"] = Path(prep["sanitized_archive"]["path"])
            for variant, archive in variants.items():
                output = directory / (variant + ".exe"); fresh(output)
                argv = runtime_argv(lld, obj, output, prof, directory / variant, archive)
                env = dict(link_env)
                if variant == "sanitized_runtime":
                    argv = sanitizer_argv(argv, prep)
                    env.update(ASAN_OPTIONS=SAN_ENV["ASAN_OPTIONS"], UBSAN_OPTIONS=SAN_ENV["UBSAN_OPTIONS"])
                linked = execute(argv, f"link:{name}:{variant}", env)
                verify_exe(output)
                observed = execute([output], f"run:{name}:{variant}", env)
                if variant == "observer":
                    if observed["stdout_hex"] != native_bytes(expected).hex():
                        raise HarnessError("observer output mismatch: " + name)
                    counts = clean_ledger(observed["stderr"], allocations)
                    observed["ledger"] = counts
                    if name == "shadow_owner":
                        events = re.findall(r"AUDIT (alloc|free) (\d+)", observed["stderr"])
                        if events != [("alloc", "5"), ("alloc", "1"), ("free", "1"), ("free", "5")]:
                            raise HarnessError("reverse scope cleanup/identity mismatch")
                    if name == "reverse_scope":
                        events = re.findall(r"AUDIT (alloc|free) (\d+)", observed["stderr"])
                        if events != [("alloc", "1"), ("alloc", "2"), ("alloc", "3"), ("free", "3"), ("free", "2"), ("free", "1")]:
                            raise HarnessError("same-scope owners were not dropped in reverse order")
                else:
                    check_run(observed, expected)
                row["variants"][variant] = {"link": linked, "run": observed, "executable": file_record(output)}
            row["dependency_audit"] = inspect(directory / "native.exe", directory, inspector)
        # OOM parity against an exact-signature C control using the SAME frozen runtime.
        oom_case = root / "owned_scope"
        oom_exe = oom_case / "oom.exe"; fresh(oom_exe)
        oom_argv = runtime_argv(lld, oom_case / "program.o", oom_exe, prof, oom_case / "oom", Path(prep["oom_archive"]["path"]))
        execute(oom_argv, "oom:link", link_env)
        observed = execute([oom_exe], "oom:run", link_env, expected=None)
        fatal = observed["stderr"].replace("\r\n", "\n")
        if observed["exit"] == 0 or observed["stdout_hex"] or "fatal: String allocation failed\n" not in fatal:
            raise HarnessError("PoC OOM lost canonical abort diagnostic")
        if sys.platform == "linux" and (observed["exit"] != -signal.SIGABRT or observed["stderr_hex"] != prep["oom_control"]["stderr_hex"]):
            raise HarnessError("OOM differs from exact runtime C control")
        report["oom"] = observed
        # Default mode and every unsupported family publish neither fresh output.
        for name, directory, cat in [("default-off", root / "reject-default-off", prepared[0][2]), *reject_prepared]:
            directory.mkdir(exist_ok=True)
            obj, ir = directory / "rejected.o", directory / "rejected.ll"
            fresh(obj); fresh(ir)
            flags = [] if name == "default-off" else ["--strings"]
            failure = execute([emitter, cat, "-o", obj, "--emit-ir", ir, "--runtime-i32", "--arithmetic-i32", *flags], "reject:" + name, emit_env, expected=None)
            ownership_rejection = name.startswith("ownership-")
            diagnostic = bool(failure["stderr_hex"]) if ownership_rejection else "LLVM C++ PoC" in failure["stderr"]
            if failure["exit"] == 0 or obj.exists() or ir.exists() or not diagnostic:
                raise HarnessError("unsupported family was not explicitly rejected before publication: " + name)
            report["rejections"].append({"name": name, "boundary": "existing Cat semantic/ownership pipeline" if ownership_rejection else "LLVM adapter", "failure": failure})
        # Real missing archive AND a valid link without the required runtime symbols.
        for name, archive in (("missing-archive", root / "absent.a"), ("missing-runtime-symbol", None)):
            directory = root / name; directory.mkdir(exist_ok=True)
            output = directory / "program.exe"; fresh(output)
            argv = runtime_argv(lld, root / "literal_str/program.o", output, prof, directory, archive)
            failure = execute(argv, "negative-link:" + name, link_env, expected=None)
            if failure["exit"] == 0 or output.exists():
                raise HarnessError("negative LLD link unexpectedly succeeded")
            if name == "missing-runtime-symbol" and "claw.runtime.println.slice" not in failure["stderr"]:
                raise HarnessError("negative link did not prove the actual undefined runtime symbol")
            report["negative_links"].append({"name": name, "failure": failure})
        changed = {path: {"before": before, "after": stamp(path)} for path, before in protected.items() if stamp(path) != before}
        if changed:
            raise HarnessError("runtime/prepared input changed: " + json.dumps(changed))
        report.update(status="PASS", immutable_inputs=protected,
                      sanitizer_scope="Instrumented C runtime and independent C host controls; emitted LLVM instructions are not all ASan-instrumented. LSan archive has no observer registry. Windows availability recorded by actual full-runtime probes.")
    except Exception as error:
        report.update(status="FAIL", error=str(error))
        raise
    finally:
        (root / "testdata.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"String gate PASS: {len(report['cases'])} cases, {len(report['rejections'])} explicit rejections")


if __name__ == "__main__":
    main()
