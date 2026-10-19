#!/usr/bin/env python3
"""Independent bounded gate for opt-in formal-parameter binding identity.

The corpus deliberately excludes tuple/pick/try binders, synthetic values and
member identity. Production Cat is the semantic oracle; the final PoC phase
uses only frozen native objects, LLD and the frozen runtime package.
"""
from __future__ import annotations
import argparse, hashlib, json, os, platform, re, shutil, sys
from pathlib import Path
if sys.platform == "linux":
    import resource
from test_strings import (runtime_argv, no_compiler, check_run, native_bytes,
    profile, isolate, verify_exe, verify_target, HarnessError, file_record,
    stamp, run, clean_ledger, SAN_ENV, sanitizer_argv)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("emitter", "production", "runtime", "external", "manifest",
                 "cc", "artifacts", "string-preparation", "git"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--expected-sha", required=True)
    p.add_argument("--profile", required=True)
    p.add_argument("--local-binding-keys", action="store_true",
                   help="retained for CMake symmetry; this gate always requires it")
    p.add_argument("--ucrt-prefix", type=Path)
    p.add_argument("--sysroot", type=Path)
    p.add_argument("--readobj", type=Path)
    a = p.parse_args()
    if not a.local_binding_keys:
        raise HarnessError("parameter gate is opt-in only: pass --local-binding-keys")
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}:
        raise HarnessError("native Linux x86_64 or Windows UCRT x64 only")
    if sys.platform == "linux":
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    root = a.artifacts.absolute(); root.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[4]
    report = {"schema":"cat-parameter-binding-identity/v1", "status":"running",
              "head":a.expected_sha, "profile":a.profile,
              "local_binding_keys":True, "scope":"formal declaration/use/drop only",
              "cases":[], "negative_cases":[], "commands":[],
              "timeout_seconds":120,
              "sanitizer": {"status":"SKIP", "provenance":"not yet probed"}}

    def execute(argv, label, env=None, expected=0):
        rec = {"phase":label, **run(argv, env=env)}
        report["commands"].append(rec)
        if expected is not None and rec["exit"] != expected:
            raise HarnessError(f"{label}: unexpected exit {rec['exit']}: {rec['stderr']}")
        return rec

    try:
        actual = execute([a.git, "-C", repo, "rev-parse", "HEAD"], "exact-head")["stdout"].strip()
        if actual != a.expected_sha:
            raise HarnessError("source HEAD changed since configure")
        data = json.loads((Path(__file__).parent / "parameter-binding-cases.json").read_text())
        positives, negatives = data["positive_cases"], data["negative_cases"]
        if len(positives) != 8 or sum(c["allocations"] for c in positives) != 13:
            raise HarnessError("parameter corpus altered: expected 8 cases and 13 allocations")
        if len(negatives) != 3:
            raise HarnessError("negative parameter corpus altered")
        manifest = json.loads(a.manifest.read_text())
        package = {role: Path(manifest[role]["path"]).resolve() for role in ("source", "object", "archive")}
        if package["archive"] != a.runtime.resolve():
            raise HarnessError("runtime manifest/archive mismatch")
        for role, path in package.items():
            if sha256(path) != manifest[role]["sha256"]:
                raise HarnessError("frozen runtime package hash mismatch: " + role)
        prep = json.loads(a.string_preparation.read_text())
        if prep.get("status") != "PASS":
            raise HarnessError("preceding String preparation unavailable")
        report["sanitizer"] = {"status":"SKIP", "provenance":prep.get("sanitizer_probes", {}),
                               "scope":"instrumented C runtime only; not all emitted LLVM instructions"}
        protected = {str(path): stamp(path) for path in (*package.values(), a.manifest.absolute(), a.string_preparation.absolute())}
        protected.update(prep.get("protected_files", {}))
        report["package"] = manifest; report["preparation"] = prep
        prof = profile(a); report["crt_inputs"] = prof["required_files"]
        emitter, emit_env, emit_iso = isolate(a.emitter.absolute(), "emitter", root / "isolated-emitter", a.readobj)
        lld, link_env, link_iso = isolate(a.external.absolute(), "external", root / "isolated-linker", a.readobj)
        no_compiler(emit_env, "parameter-emission"); no_compiler(link_env, "parameter-link")
        run_env = os.environ.copy(); run_env["PATH"] = ""
        report["isolation"] = {"emitter":emit_iso, "linker":link_iso, "compiler_reachable":False}
        expected_native = lambda text: native_bytes(text.encode()).hex()

        for case in positives:
            name = case["name"]; d = root / name; d.mkdir(parents=True, exist_ok=True)
            cat = d / (name + ".cat"); cat.write_text(case["source"], encoding="utf-8")
            row = {"name":name, "source":file_record(cat),
                   "expected_stdout_hex":expected_native(case["stdout"]),
                   "allocations_per_execution":case["allocations"], "traces":{}, "profiles":{}}
            for stage in ("check", "oir", "lir", "llvm"):
                trace = execute([a.production, stage, cat], f"{name}:{stage}")
                row["traces"][stage] = trace
                (d / (stage + ".txt")).write_bytes(bytes.fromhex(trace["stdout_hex"]))
            for label, build_flags in (("O0", []), ("O2", ["--release"])):
                exe = d / (label + ".production.exe")
                built = execute([a.production, "build", *build_flags, cat, exe], f"{name}:{label}:build")
                observed = execute([exe], f"{name}:{label}:run", env=run_env)
                check_run(observed, case["stdout"].encode())
                if sys.platform == "linux":
                    hdr = exe.read_bytes()[:20]
                    if hdr[:6] != b"\x7fELF\x02\x01" or int.from_bytes(hdr[16:18], "little") not in (2,3) or int.from_bytes(hdr[18:20], "little") != 62:
                        raise HarnessError(f"{name}: production output is not native ELF64")
                else:
                    verify_exe(exe)
                row["profiles"][label] = {"build":built, "run":observed, "executable":file_record(exe)}
                # Reference-only compilation of production LLVM output. This is
                # not the experimental emission/link path, which stays compiler-free.
                reference = d / (label + ".reference.o")
                compiled = execute([a.cc, "-Wno-override-module", "-O2" if label == "O2" else "-O0",
                                    "-c", exe.with_suffix(".ll"), "-o", reference], f"{name}:{label}:reference-object")
                variants = {}
                for variant, lib in [("observer", prep["observer_archive"]),
                        *([("sanitized_runtime", prep["sanitized_archive"])] if prep.get("sanitized_archive") else [])]:
                    target = d / (label + "." + variant + ".exe")
                    argv = runtime_argv(lld, reference, target, prof, d / (label + "-" + variant), Path(lib["path"]))
                    if variant == "sanitized_runtime": argv = sanitizer_argv(argv, prep)
                    linked = execute(argv, f"{name}:{label}:{variant}:LLD", env=link_env)
                    result = execute([target], f"{name}:{label}:{variant}:run",
                                     env={**run_env, **SAN_ENV} if variant == "sanitized_runtime" else run_env)
                    if variant == "observer":
                        if result["stdout_hex"] != expected_native(case["stdout"]):
                            raise HarnessError(f"{name}:{label}: observer stdout mismatch")
                        ledger = clean_ledger(result["stderr"], case["allocations"])
                        variants[variant] = {"link":linked, "run":result, "ledger":ledger}
                    else:
                        check_run(result, case["stdout"].encode())
                        variants[variant] = {"link":linked, "run":result}
                row["profiles"][label].update(reference_compile=compiled, variants=variants)
            obj, ir = d / "program.o", d / "program.ll"
            obj.unlink(missing_ok=True); ir.unlink(missing_ok=True)
            # Default-off preservation follows the measured historical outcome;
            # String formals were already accepted with --strings, using names.
            old = execute([emitter, cat, "--strings", "--runtime-i32", "--arithmetic-i32",
                           "-o", obj, "--emit-ir", ir], f"{name}:default-off", env=emit_env, expected=None)
            if case["baseline_poc"] == "reject":
                if old["exit"] == 0 or obj.exists() or ir.exists():
                    raise HarnessError(f"{name}: default-off unexpectedly emitted output")
                row["default_off"] = {"status":"reject", "record":old, "no_object_or_ir":True}
            else:
                if old["exit"] != 0:
                    raise HarnessError(f"{name}: default-off scalar preservation failed")
                old_target = verify_target(obj, ir); old_exe = d / "default-off.exe"
                linked = execute(runtime_argv(lld, obj, old_exe, prof, d / "default-off-link", package["archive"]), f"{name}:default-off:LLD", env=link_env)
                ran = execute([old_exe], f"{name}:default-off:run", env=run_env); check_run(ran, case["stdout"].encode())
                row["default_off"] = {"status":"pass", "target":old_target, "link":linked, "run":ran}
                obj.unlink(missing_ok=True); ir.unlink(missing_ok=True)
            # The opt-in adapter receives only native object output from here.
            emitted = execute([emitter, cat, "--strings", "--runtime-i32", "--arithmetic-i32",
                               "--local-binding-keys", "-o", obj, "--emit-ir", ir],
                              f"{name}:PoC:emit", env=emit_env)
            target = verify_target(obj, ir)
            row["poc"] = {"emit":emitted, "target":target, "object":file_record(obj), "ir":file_record(ir)}
            exe = d / "poc.exe"
            linked = execute(runtime_argv(lld, obj, exe, prof, d / "poc-link", package["archive"]), f"{name}:PoC:LLD", env=link_env)
            ran = execute([exe], f"{name}:PoC:run", env=run_env); check_run(ran, case["stdout"].encode())
            row["poc"].update(link=linked, run=ran)
            variants = {}
            observer = d / "poc.observer.exe"
            olink = execute(runtime_argv(lld, obj, observer, prof, d / "poc-observer-link", Path(prep["observer_archive"]["path"])), f"{name}:PoC:observer:LLD", env=link_env)
            orun = execute([observer], f"{name}:PoC:observer:run", env=run_env)
            if orun["stdout_hex"] != expected_native(case["stdout"]):
                raise HarnessError(f"{name}: observer stdout mismatch")
            variants["observer"] = {"link":olink, "run":orun, "ledger":clean_ledger(orun["stderr"], case["allocations"])}
            if prep.get("sanitized_archive"):
                san = d / "poc.sanitized.exe"
                slink = execute(sanitizer_argv(runtime_argv(lld, obj, san, prof, d / "poc-sanitized-link", Path(prep["sanitized_archive"]["path"])), prep), f"{name}:PoC:sanitized:LLD", env=link_env)
                srun = execute([san], f"{name}:PoC:sanitized:run", env={**run_env, **SAN_ENV})
                check_run(srun, case["stdout"].encode())
                variants["sanitized_runtime"] = {"link":slink, "run":srun}
                report["sanitizer"] = {"status":"PASS", "provenance":prep.get("sanitizer_probes", {}),
                                       "scope":"instrumented C runtime only; not all emitted LLVM instructions"}
            row["poc"]["variants"] = variants
            report["cases"].append(row)

        for case in negatives:
            name = case["name"]; d = root / ("negative-" + name); d.mkdir(parents=True, exist_ok=True)
            cat = d / (name + ".cat"); cat.write_text(case["source"], encoding="utf-8")
            output = d / "rejected.exe"; output.unlink(missing_ok=True)
            failure = execute([a.production, "build", cat, output], f"negative:{name}", expected=None)
            text = failure["stderr"].lower()
            if failure["exit"] == 0 or output.exists():
                raise HarnessError(f"{name}: compiler accepted invalid ownership/parameter case")
            for fragment in case["diagnostic_fragments"]:
                if fragment.lower() not in text:
                    raise HarnessError(f"{name}: diagnostic missing fragment {fragment!r}")
            obj, ir = d / (name + ".o"), d / (name + ".ll")
            obj.unlink(missing_ok=True); ir.unlink(missing_ok=True)
            denied = execute([emitter, cat, "--strings", "--runtime-i32", "--arithmetic-i32",
                              "--local-binding-keys", "-o", obj, "--emit-ir", ir],
                             f"negative:{name}:PoC", env=emit_env, expected=None)
            if denied["exit"] == 0 or obj.exists() or ir.exists():
                raise HarnessError(f"{name}: PoC accepted invalid source or published output")
            for fragment in case["diagnostic_fragments"]:
                if fragment.lower() not in denied["stderr"].lower():
                    raise HarnessError(f"{name}: PoC diagnostic missing fragment {fragment!r}")
            report["negative_cases"].append({"name":name, "diagnostic":failure, "poc":denied, "no_object_or_ir":True})

        # Invalid opt-in combinations must remain fail-closed and artifact-free.
        control_dir = root / "controls"; control_dir.mkdir(exist_ok=True)
        control_cat = control_dir / "control.cat"; control_cat.write_text(positives[1]["source"])
        controls = []
        for label, flags in (("missing-strings", ["--local-binding-keys"]),
                             ("duplicate-local-flag", ["--strings", "--local-binding-keys", "--local-binding-keys"])):
            o, l = control_dir / (label + ".o"), control_dir / (label + ".ll")
            o.unlink(missing_ok=True); l.unlink(missing_ok=True)
            rec = execute([emitter, control_cat, *flags, "-o", o, "--emit-ir", l], "control:" + label, env=emit_env, expected=None)
            if rec["exit"] == 0 or o.exists() or l.exists():
                raise HarnessError("invalid local-binding-keys combination bypassed rejection: " + label)
            controls.append({"name":label, "record":rec, "no_object_or_ir":True})
        report["invalid_flag_controls"] = controls
        for path, before in protected.items():
            if stamp(Path(path)) != before:
                raise HarnessError("immutable frozen runtime/preparation changed: " + path)
        report["immutable_inputs"] = protected
        report["observer_total_per_profile"] = sum(c["allocations"] for c in positives)
        report["observer_total_both_production_profiles"] = 2 * sum(c["allocations"] for c in positives)
        report["observer_total_with_poc"] = 3 * sum(c["allocations"] for c in positives)
        report["status"] = "PASS"
    except Exception as exc:
        report["status"] = "FAIL"; report["error"] = str(exc)
        raise
    finally:
        (root / "testdata.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS parameter binding keys: 8 bounded Cat cases; production O0/O2; 13 observer allocations; negatives and fail-closed controls")


if __name__ == "__main__":
    main()
