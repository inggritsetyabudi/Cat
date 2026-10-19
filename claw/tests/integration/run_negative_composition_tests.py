#!/usr/bin/env python3
"""Standalone v0.1-v0.3 negative composition matrix.

Usage: CLAW_EXE=./claw/build/claw python3 claw/tests/integration/run_negative_composition_tests.py
or: python3 .../run_negative_composition_tests.py /path/to/claw
"""
from __future__ import annotations
import json, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
NEGATIVE = ROOT / "negative"

def main() -> int:
    fallback = ROOT.parents[1] / "build" / "claw"
    if not fallback.is_file():
        fallback = ROOT.parents[1] / "build-ucrt64-clang" / "claw.exe"
    compiler = Path(os.environ.get("CLAW_EXE", sys.argv[1] if len(sys.argv) > 1 else str(fallback))).resolve()
    if not compiler.is_file():
        print(f"FAIL compiler not found: {compiler}", file=sys.stderr)
        return 2
    cases = json.loads((NEGATIVE / "manifest.json").read_text(encoding="utf-8"))
    env = dict(os.environ, NO_COLOR="1")
    passed = 0
    with tempfile.TemporaryDirectory(prefix="claw-negative-composition-") as td:
        root = Path(td)
        for case in cases:
            name = case["name"]
            source = NEGATIVE / f"{name}.cat"
            for mode in ("check", "oir", "llvm", "build"):
                copied = root / f"{name}-{mode}.cat"
                copied.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
                exe = root / f"{name}-{mode}.exe"
                args = [str(compiler), mode, str(copied)]
                if mode == "build": args.append(str(exe))
                result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", env=env, timeout=30)
                stderr = result.stderr.replace("\r", "")
                location = f"{copied.name}:{case['line']}:{case['column']}"
                headers = re.findall(r"^error(?:\[[^\]]+\])?: (.*)$", stderr, re.MULTILINE)
                final_failure = f"build failed for `{copied.stem}`"
                if mode == "build":
                    exact_diagnostic = (headers.count(case["diagnostic"]) == 1
                                        and headers.count(final_failure) == 1
                                        and len(headers) == 2)
                else:
                    exact_diagnostic = (headers == [case["diagnostic"]]
                                        and re.search(r"^error\[[^\]]+\]: " +
                                                      re.escape(case["diagnostic"]),
                                                      stderr, re.MULTILINE) is not None)
                exact_location = re.search(r"^ --> .*[/\\]" + re.escape(location) + r"$", stderr, re.MULTILINE)
                ok = (result.returncode > 0 and result.stdout == ""
                      and exact_diagnostic and exact_location
                      and not exe.exists() and not exe.with_suffix(".ll").exists()
                      and not copied.with_suffix(".ll").exists())
                if not ok:
                    print(f"FAIL {name}/{mode}: expected {case['diagnostic']!r} at {location}, no stdout/artifacts", file=sys.stderr)
                    print(f"exit={result.returncode}\nstdout={result.stdout!r}\nstderr={stderr}", file=sys.stderr)
                    return 1
                passed += 1
            print(f"[negative/pass] {name}: check/OIR/LLVM/native-build")
    print(f"Negative composition matrix: {len(cases)} cases, {passed} command checks passed.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
