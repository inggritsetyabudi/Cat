#!/usr/bin/env python3
"""Compare the unchanged legacy emitter corpus against the b148 emitter.

This is deliberately an emitter-only gate: it never passes --pair-i32, never
builds or links, and keeps the same probe.cat path for each baseline/current
pair while writing distinct object and IR artifacts.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_poc import isolated_windows_emitter  # noqa: E402
from test_singleton_native import MODES as SINGLETON_MODES  # noqa: E402
from test_singleton_native import NEGATIVES as SINGLETON_NEGATIVES  # noqa: E402
from test_singleton_native import POSITIVES as SINGLETON_POSITIVES  # noqa: E402
from runtime.test_arithmetic import CASES as ARITHMETIC_CASES  # noqa: E402
from runtime.test_arithmetic import EXTRA as ARITHMETIC_EXTRA  # noqa: E402
from runtime.test_arithmetic import production_source as arithmetic_source  # noqa: E402


class HarnessError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_record(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": sha256(data)}


def command(argv, env):
    result = subprocess.run([str(x) for x in argv], capture_output=True, env=env, timeout=180)
    return {
        "argv": [str(x) for x in argv],
        "exit": result.returncode,
        "stdout_hex": result.stdout.hex(),
        "stderr_hex": result.stderr.hex(),
        "stdout": result.stdout.decode("utf-8", "replace"),
        "stderr": result.stderr.decode("utf-8", "replace"),
    }, result


def _literal_dict(path: Path, name: str) -> dict:
    """Extract a local dictionary assignment without executing test_poc.main."""
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in ast.walk(tree):
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if any(isinstance(t, ast.Name) and t.id == name for t in targets):
                value = node.value
                try:
                    result = ast.literal_eval(value)
                except (ValueError, TypeError, SyntaxError) as exc:
                    raise HarnessError(f"cannot extract literal {name} from {path}: {exc}") from exc
                if not isinstance(result, dict):
                    raise HarnessError(f"extracted {name} is not a dictionary")
                return result
    raise HarnessError(f"dictionary {name} not found in {path}")


def legacy_corpus() -> tuple[dict, dict]:
    path = HERE / "test_poc.py"
    return _literal_dict(path, "successes"), _literal_dict(path, "negatives")


def normalized_source(source: str, semicolon_style: bool = False) -> str:
    # Match the historical runners' source preparation for their local literals.
    return (source.replace(";", "\n") if semicolon_style else source) + ("" if source.endswith("\n") else "\n")


def linux_env() -> dict:
    env = os.environ.copy()
    env["PATH"] = ""
    env["CC"] = env["CXX"] = env["CLAW_NATIVE_CLANG"] = "/__cat_missing_compiler__"
    return env


def prepare_emitters(args, root: Path) -> dict[str, tuple[Path, dict]]:
    result = {}
    for label, original in (("baseline", args.baseline), ("current", args.emitter)):
        if not original.is_file():
            raise HarnessError(f"{label} emitter is missing: {original}")
        if sys.platform == "win32":
            if not args.readobj:
                raise HarnessError("Windows requires --readobj for emitter dependency isolation")
            # The helper owns a child directory; make its caller-owned parent first.
            parent = root / f"{label}-emitter"
            parent.mkdir(parents=True, exist_ok=True)
            isolation, env = isolated_windows_emitter(parent, original, args.readobj)
            path = isolation / original.name
            if not path.is_file():
                raise HarnessError(f"isolated {label} emitter is missing: {path}")
            result[label] = (path, env)
        else:
            result[label] = (original.resolve(), linux_env())
    return result


def fresh(path: Path) -> None:
    path.unlink(missing_ok=True)


def emit_one(binary: Path, env: dict, cat: Path, obj: Path, ir: Path, flags=(), clean=True):
    if clean:
        fresh(obj); fresh(ir)
    argv = [binary, cat, "-o", obj, "--emit-ir", ir, *flags]
    if any(str(x) == "--pair-i32" for x in argv):
        raise HarnessError("pair flag leaked into legacy adapter")
    row, raw = command(argv, env)
    return row, raw


def compare_success(root: Path, item: dict, emitters: dict[str, tuple[Path, dict]], report: dict) -> None:
    case = root / "success" / item["suite"] / item["name"] / item["mode"]
    case.mkdir(parents=True, exist_ok=True)
    cat = case / "probe.cat"
    cat.write_text(item["source"], encoding="utf-8")
    rows = {}
    # Sequential runs intentionally reuse the exact same probe.cat path.
    for label in ("baseline", "current"):
        obj, ir = case / f"{label}.o", case / f"{label}.ll"
        row, raw = emit_one(*emitters[label], cat, obj, ir, item["flags"])
        if row["exit"] != 0 or not obj.is_file() or not ir.is_file():
            raise HarnessError(f"{item['suite']}/{item['name']}/{item['mode']} {label} did not emit successfully: {row}")
        rows[label] = {"emit": row, "object": file_record(obj), "ir": file_record(ir)}
    for kind in ("object", "ir"):
        if rows["baseline"][kind]["sha256"] != rows["current"][kind]["sha256"]:
            raise HarnessError(f"byte mismatch: {item['suite']}/{item['name']}/{item['mode']} {kind}")
    report["successes"].append({**item, "path": str(cat), "baseline": rows["baseline"], "current": rows["current"]})


def reject_one(binary: Path, env: dict, cat: Path, obj: Path, ir: Path, label: str, flags=(), clean=True) -> dict:
    row, _ = emit_one(binary, env, cat, obj, ir, flags, clean)
    if row["exit"] == 0 or (clean and (obj.exists() or ir.exists())):
        raise HarnessError(f"{label}: rejection unexpectedly succeeded/published artifacts: {row}")
    return row


def compare_rejection(root: Path, item: dict, emitters: dict[str, tuple[Path, dict]], report: dict) -> None:
    case = root / "rejection" / item["suite"] / item["name"] / item["mode"]
    case.mkdir(parents=True, exist_ok=True)
    cat = case / "probe.cat"; cat.write_text(item["source"], encoding="utf-8")
    bo, bi = case / "baseline.o", case / "baseline.ll"
    baseline, _ = emit_one(*emitters["baseline"], cat, bo, bi, item["flags"])
    baseline_success = baseline["exit"] == 0 and bo.is_file() and bi.is_file()
    if baseline_success:
        co, ci = case / "current.o", case / "current.ll"
        current, _ = emit_one(*emitters["current"], cat, co, ci, item["flags"])
        if current["exit"] != 0 or not co.is_file() or not ci.is_file():
            raise HarnessError(f"baseline accepted but current rejected: {item['name']}/{item['mode']}: {current}")
        for a, b, kind in ((bo, co, "object"), (bi, ci, "ir")):
            if a.read_bytes() != b.read_bytes():
                raise HarnessError(f"byte mismatch: {item['suite']}/{item['name']}/{item['mode']} {kind}")
        report["successes"].append({**item, "oracle": "baseline-accepted", "path": str(cat),
            "baseline": {"emit": baseline, "object": file_record(bo), "ir": file_record(bi)},
            "current": {"emit": current, "object": file_record(co), "ir": file_record(ci)}})
        return
    if baseline["exit"] == 0 or bo.exists() or bi.exists():
        raise HarnessError(f"baseline rejection did not clean artifacts: {item['name']}/{item['mode']}")
    rows = {"baseline": {"absent": baseline}}
    co, ci = case / "current.o", case / "current.ll"
    current = reject_one(*emitters["current"], cat, co, ci, f"{item['name']}/current/absent", item["flags"], True)
    rows["current"] = {"absent": current}
    if baseline["exit"] != current["exit"]:
        raise HarnessError(f"rejection exit mismatch: {item['suite']}/{item['name']}/{item['mode']}")
    for label, paths in (("baseline", (bo, bi)), ("current", (co, ci))):
        obj, ir = paths; so = f"LEGACY {label} OBJECT SENTINEL\n".encode(); si = f"LEGACY {label} IR SENTINEL\n".encode()
        obj.write_bytes(so); ir.write_bytes(si)
        row = reject_one(*(emitters[label]), cat, obj, ir, f"{item['name']}/{label}/sentinel", item["flags"], False)
        if obj.read_bytes() != so or ir.read_bytes() != si:
            raise HarnessError(f"{item['name']}/{label}: rejection clobbered sentinel")
        rows[label]["sentinel"] = row; fresh(obj); fresh(ir)
    report["rejections"].append({**item, "oracle": "baseline-rejected", "path": str(cat), **rows})

def build_cases() -> tuple[list[dict], list[dict]]:
    successes, negatives = legacy_corpus()
    good, bad = [], []
    # Historical test_poc corpus is run in its default and legacy flag modes.
    legacy_modes = {"default": (), "legacy": ("--runtime-i32", "--arithmetic-i32", "--strings")}
    for name, (source, _expected) in successes.items():
        for mode, flags in legacy_modes.items():
            good.append({"suite": "test_poc", "name": name, "mode": mode, "flags": list(flags), "source": normalized_source(source, True)})
    for name, source in negatives.items():
        for mode, flags in legacy_modes.items():
            bad.append({"suite": "test_poc", "name": name, "mode": mode, "flags": list(flags), "source": normalized_source(source, True)})
    for name, source in SINGLETON_POSITIVES.items():
        for mode, flags in SINGLETON_MODES.items():
            good.append({"suite": "singleton", "name": name, "mode": mode, "flags": list(flags) + ["--singleton-i32"], "source": source})
    for name, source in SINGLETON_NEGATIVES.items():
        for mode, flags in SINGLETON_MODES.items():
            bad.append({"suite": "singleton", "name": name, "mode": mode, "flags": list(flags) + ["--singleton-i32"], "source": source})
    arithmetic_flags = ["--runtime-i32", "--arithmetic-i32"]
    for name, (op, a, b, _result) in ARITHMETIC_CASES.items():
        source = arithmetic_source(name)
        good.append({"suite": "arithmetic", "name": name, "mode": "runtime-arithmetic", "flags": arithmetic_flags, "source": source})
    for name, (source, expected, _code) in ARITHMETIC_EXTRA.items():
        good.append({"suite": "arithmetic", "name": name, "mode": "runtime-arithmetic", "flags": arithmetic_flags, "source": normalized_source(arithmetic_source(name), False)})
    return good, bad


def main(args) -> int:
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform not in {"linux", "win32"}:
        raise HarnessError("native x86_64 Linux/Windows only")
    root = args.artifacts.resolve(); root.mkdir(parents=True, exist_ok=True)
    emitters = prepare_emitters(args, root)
    good, bad = build_cases()
    report = {"schema": "cat-pair-legacy/v1", "status": "RUNNING", "baseline": str(args.baseline.resolve()), "emitter": str(args.emitter.resolve()), "platform": sys.platform, "successes": [], "rejections": [], "case_counts": {"successes": len(good), "rejections": len(bad)}}
    evidence = root / "evidence.json"
    try:
        for item in good:
            compare_success(root, item, emitters, report)
        for item in bad:
            compare_rejection(root, item, emitters, report)
        report["status"] = "PASS"
    except Exception as exc:
        report.update(status="FAIL", error=str(exc))
        evidence.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        raise
    evidence.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS legacy emitter pair: {len(good)} successes + {len(bad)} rejections; evidence={evidence}")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True, help="b148 legacy emitter")
    parser.add_argument("--emitter", type=Path, required=True, help="current emitter")
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--readobj", type=Path, help="llvm-readobj for Windows DLL isolation")
    try:
        raise SystemExit(main(parser.parse_args()))
    except HarnessError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
