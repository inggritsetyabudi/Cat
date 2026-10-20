"""Preparation-only C tooling for the bounded String gate; never called per source."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
if sys.platform == "linux":
    import resource

ORACLE = Path(__file__).resolve().parents[1] / "abi_oracle"
SELFTEST = ORACLE / "observer_selftest.c"
SAN_ENV = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1", "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}


def stamp(path):
    path = Path(path).resolve()
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return {"path": str(path), "bytes": path.stat().st_size,
            "mtime_ns": path.stat().st_mtime_ns, "sha256": h.hexdigest()}


def run(argv, env=None, cwd=None):
    started = time.perf_counter_ns()
    p = subprocess.run(list(map(str, argv)), capture_output=True, env=env, cwd=cwd, timeout=120)
    return {"argv": list(map(str, argv)), "exit": p.returncode,
            "stdout_hex": p.stdout.hex(), "stderr_hex": p.stderr.hex(),
            "stdout": p.stdout.decode("utf-8", "replace"), "stderr": p.stderr.decode("utf-8", "replace"),
            "elapsed_ms": (time.perf_counter_ns() - started) / 1e6}


def must(record, label):
    if record["exit"]:
        raise RuntimeError(f"{label}: {record['argv']}\n{record['stderr']}")
    return record


def ledger(stderr):
    matches = re.findall(r"AUDIT summary allocations=(\d+) frees=(\d+) outstanding=(\d+) untracked_frees=(\d+)", stderr)
    if len(matches) != 1:
        raise RuntimeError("observer requires one unique summary")
    return dict(zip(("allocations", "frees", "outstanding", "untracked_frees"), map(int, matches[0])))


def clean_ledger(stderr, count):
    actual = ledger(stderr)
    expected = dict(allocations=count, frees=count, outstanding=0, untracked_frees=0)
    if actual != expected:
        raise RuntimeError(f"observer mismatch {actual}, expected {expected}")
    if len(re.findall(r"^AUDIT alloc \d+\r?$", stderr, re.M)) != count or len(re.findall(r"^AUDIT free \d+\r?$", stderr, re.M)) != count:
        raise RuntimeError("observer event count differs from summary")
    if re.search(r"ERROR: .*Sanitizer|runtime error:|LeakSanitizer:", stderr):
        raise RuntimeError("sanitizer diagnostic in clean run")
    return actual


def sanitizer_argv(base, prep):
    """Extend an already-resolved LLD argv; no compiler executable is involved."""
    argv = list(base)
    index = argv.index("--start-group")
    argv[index:index] = prep["san_link_inputs"] + ["--no-as-needed"] + prep["san_link_tail"]
    return argv


def prepare(cc, ar, frozen_runtime, root, lld, prof):
    cc, ar, frozen_runtime, root, lld = map(lambda x: Path(x).absolute(), (cc, ar, frozen_runtime, root, lld))
    root.mkdir(parents=True, exist_ok=True)
    if sys.platform == "linux":
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    records = []
    result = {"schema": "cat-string-preparation/v3", "status": "running", "platform": sys.platform,
              "compiler": str(cc), "ar": str(ar), "lld": str(lld), "frozen_runtime": stamp(frozen_runtime),
              "records": records, "protected_files": {}}
    def step(argv, label, required=True, env=None):
        record = {"phase": label, **run(argv, env=env, cwd=root)}
        records.append(record)
        if required:
            must(record, label)
        return record
    def protect(path):
        result["protected_files"][str(Path(path).resolve())] = stamp(path)
    def archive(source, name, flags=()):
        obj, lib = root / (name + ".o"), root / (name + ".a")
        step([cc, "-std=gnu11", "-O0", "-ffunction-sections", "-fdata-sections", "-fno-lto", *flags, "-c", source, "-o", obj], name + ":compile")
        lib.unlink(missing_ok=True)
        step([ar, "rcs", lib, obj], name + ":archive")
        protect(source); protect(obj); protect(lib)
        return lib
    try:
        result["compiler_version"] = step([cc, "--version"], "compiler-version")
        result["compiler_target"] = step([cc, "-dumpmachine"], "compiler-target")
        protect(frozen_runtime); protect(SELFTEST); protect(ORACLE / "observer_runtime.c")
        # Preserve the already-reviewed observer, including overflow/atexit guards.
        original = (ORACLE / "observer_runtime.c").read_text()
        include = '#include "../../../runtime/native_runtime.c"'
        if original.count(include) != 1:
            raise RuntimeError("unexpected observer include contract")
        frozen_include = '#include "' + frozen_runtime.as_posix() + '"'
        observer = root / "observer-frozen.c"
        observer.write_text(original.replace(include, frozen_include))
        obs_lib = archive(observer, "observer-runtime")
        result["observer_archive"] = stamp(obs_lib)
        oom_source = root / "oom-frozen.c"
        oom_source.write_text('#include <stddef.h>\n#include <stdlib.h>\nstatic void *forced_malloc(size_t n) { (void)n; return NULL; }\n#define malloc forced_malloc\n' + frozen_include + '\n#undef malloc\n')
        oom_lib = archive(oom_source, "oom-runtime")
        result["oom_archive"] = stamp(oom_lib)
        # Preparation controls: real malloc/free; deliberately bad ledgers must not pass.
        control_obj = root / "observer-selftest.o"
        step([cc, "-std=gnu11", "-O0", "-c", SELFTEST, "-o", control_obj], "control:compile")
        control_exe = root / "observer-control.exe"
        step([cc, control_obj, obs_lib, "-o", control_exe], "control:link")
        expected = {"normal": (1, 1, 0, 0), "leak": (1, 0, 1, 0), "untracked": (1, 1, 0, 1)}
        result["controls"] = {}
        for mode, counts in expected.items():
            record = step([control_exe, mode], "control:" + mode)
            got = ledger(record["stderr"])
            if tuple(got.values()) != counts:
                raise RuntimeError(f"{mode} control wrong ledger: {got}")
            if mode != "normal":
                try:
                    clean_ledger(record["stderr"], 1)
                except RuntimeError:
                    record["clean_validator_rejected"] = True
                else:
                    raise RuntimeError(f"{mode} negative control was accepted")
            result["controls"][mode] = record
        # Exact int64_t declaration, including on Win64 (where C long is 32 bits).
        oom_caller = root / "oom-caller.c"
        oom_caller.write_text('#include <stdint.h>\nextern void *alloc_string(int64_t) __asm__("claw.runtime.string.alloc");\nint main(void) { return alloc_string(1) != 0; }\n')
        oom_exe = root / "oom-control.exe"
        step([cc, "-std=gnu11", oom_caller, oom_lib, "-o", oom_exe], "oom-control:build")
        oom_run = step([oom_exe], "oom-control:run", required=False)
        if not oom_run["exit"] or "fatal: String allocation failed\n" not in oom_run["stderr"].replace("\r\n", "\n") or oom_run["stdout_hex"]:
            raise RuntimeError("OOM control lost canonical fatal/abort behavior")
        result["oom_control"] = oom_run
        # Every availability probe includes the real runtime TU and allocating caller.
        # A compile/link failure on Windows is a recorded SKIP, never PASS.
        result["sanitizer_probes"] = {}
        for flags in ("address", "undefined", "leak", "address,undefined,leak"):
            exe = root / ("probe-" + flags.replace(",", "-") + ".exe")
            built = step([cc, "-std=gnu11", "-O0", "-g", "-fno-omit-frame-pointer", "-fsanitize=" + flags,
                          SELFTEST, frozen_runtime, "-o", exe], "sanitizer:" + flags + ":build", required=False)
            observed = step([exe, "normal"], "sanitizer:" + flags + ":run", required=False, env=SAN_ENV) if built["exit"] == 0 else None
            ok = bool(observed and observed["exit"] == 0)
            if ok:
                if observed["stderr_hex"] or observed["stdout"].replace("\r\n", "\n") != "observer-selftest:ok\n":
                    raise RuntimeError("sanitizer C control produced unexpected output")
            unavailable = sys.platform == "win32" and (built["exit"] != 0 or (observed and (observed["exit"] & 0xffffffff) in (0xC0000135, 0xC000007B)))
            status = "PASS" if ok else ("SKIP" if unavailable else "FAIL")
            result["sanitizer_probes"][flags] = {"status": status, "build": built, "run": observed,
                "scope": "C host + exact runtime instrumentation, not all generated LLVM instructions"}
            if status == "FAIL":
                raise RuntimeError(f"required Linux sanitizer probe failed: {flags}")
        result["san_link_inputs"], result["san_link_tail"] = [], []
        if sys.platform == "linux":
            # The sanitizer variant MUST NOT contain observer_slots: that global
            # registry would keep leaked pointers reachable and mask LSan errors.
            san_lib = archive(frozen_runtime, "runtime-san", ("-fno-omit-frame-pointer", "-fsanitize=address,undefined,leak"))
            result["sanitized_archive"] = stamp(san_lib)
            for name in ("libclang_rt.asan_static-x86_64.a", "libclang_rt.asan-x86_64.a"):
                path = Path(step([cc, "-print-file-name=" + name], "resolve:" + name)["stdout"].strip())
                if not path.is_absolute() or not path.is_file():
                    raise RuntimeError(f"missing concrete sanitizer runtime {path}")
                result["san_link_inputs"] += ["--whole-archive", str(path), "--no-whole-archive"]
                protect(path)
                syms = Path(str(path) + ".syms")
                if syms.is_file():
                    result["san_link_inputs"].append("--dynamic-list=" + str(syms)); protect(syms)
            result["san_link_tail"] = ["-lpthread", "-lrt", "-lm", "-ldl", "-lresolv"]
            # Prove these are usable with LLD now, not just a reported argv guess.
            from lld.test_lld import link_argv
            san_control = root / "lld-san-control.exe"
            argv = link_argv(lld, control_obj, san_control, prof, root / "lld-san-control.map")
            idx = argv.index("--start-group")
            argv[idx:idx] = [str(san_lib)]
            argv = sanitizer_argv(argv, result)
            result["lld_sanitizer_control_link"] = step(argv, "lld-sanitizer-control:link")
            observed = step([san_control, "normal"], "lld-sanitizer-control:run", env=SAN_ENV)
            if observed["stderr_hex"] or observed["stdout"] != "observer-selftest:ok\n":
                raise RuntimeError("LLD sanitizer control unexpected output")
            result["lld_sanitizer_control_run"] = observed
            leaked = step([san_control, "leak"], "lld-sanitizer-control:leak", required=False, env=SAN_ENV)
            if leaked["exit"] == 0 or "LeakSanitizer: detected memory leaks" not in leaked["stderr"]:
                raise RuntimeError("LLD LSan negative control failed to detect a real leak")
            result["lld_sanitizer_leak_control"] = leaked
        result["status"] = "PASS"
        return result
    except Exception as error:
        result.update(status="FAIL", error=str(error))
        raise
    finally:
        (root / "preparation.json").write_text(json.dumps(result, indent=2) + "\n")
