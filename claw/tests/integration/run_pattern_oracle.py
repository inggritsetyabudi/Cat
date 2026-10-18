#!/usr/bin/env python3
"""Finite, generated oracle for the supported choice and tuple pick subset.

This is deliberately not a general exhaustiveness proof.  It independently
matches generated patterns against small structural universes, checks both
acceptance and the compiler's diagnostic text, and builds/runs a bounded
subset of accepted programs to compare exact stdout.
"""
from __future__ import annotations

import itertools
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from dataclasses import dataclass

SEED = 0xC0FFEE
CASES = 96

@dataclass(frozen=True)
class Case:
    name: str
    source: str
    expected_accept: bool
    universe: frozenset[str]
    expected_diagnostic: str | None = None
    build: bool = False
    expected_stdout: str = ""


def choice_covered(pattern: str, universe: frozenset[str]) -> frozenset[str]:
    """Match a choice tag (including payload-binding syntax) structurally."""
    p = pattern.strip()
    if p == "_":
        return universe
    tag = p.split("(", 1)[0]
    return frozenset(v for v in universe if v.split("/", 1)[0] == tag)


def tuple_shape(pattern: str) -> tuple | str:
    """Parse only the supported binding/wildcard tuple pattern shape."""
    tokens = pattern.replace("(", " ( ").replace(")", " ) ").replace(",", " , ").split()
    pos = 0
    def parse():
        nonlocal pos
        if pos >= len(tokens):
            raise ValueError("incomplete tuple pattern")
        if tokens[pos] != "(":
            pos += 1
            return "field"
        pos += 1
        fields = [parse()]
        while pos < len(tokens) and tokens[pos] == ",":
            pos += 1
            fields.append(parse())
        if pos >= len(tokens) or tokens[pos] != ")":
            raise ValueError("unclosed tuple pattern")
        pos += 1
        return tuple(fields)
    result = parse()
    if pos != len(tokens):
        raise ValueError("trailing tuple pattern")
    return result


def tuple_covered(pattern: str, universe: frozenset[str]) -> frozenset[str]:
    """Match tuple patterns by arity/nesting, not by blanket parentheses."""
    if pattern.strip() == "_":
        return universe
    try:
        shape = tuple_shape(pattern)
    except ValueError:
        return frozenset()
    return frozenset(v for v in universe if v == repr(shape))


def finite_oracle(patterns: list[str], universe: frozenset[str], kind: str):
    seen: set[str] = set()
    wildcard_seen = False
    for pattern in patterns:
        if wildcard_seen:
            return False, frozenset(seen), pattern
        hit = (choice_covered(pattern, universe) if kind == "choice"
               else tuple_covered(pattern, universe))
        if not hit:
            return False, frozenset(seen), pattern
        if hit <= seen and not (kind == "choice" and pattern.strip() == "_"):
            return False, frozenset(seen), pattern
        seen.update(hit)
        wildcard_seen = pattern.strip() == "_"
    return seen == set(universe), frozenset(seen), None


def choice_case(i: int, rng: random.Random) -> Case:
    count = 2 + (i % 4)
    tags = [f"v{n}" for n in range(count)]
    payload = (i % 2 == 1)  # independent of case selection
    universe = frozenset(f"{tag}/payload" for tag in tags)
    mode = i % 6
    if mode == 0:
        patterns = tags[:]; rng.shuffle(patterns)
    elif mode == 1:
        patterns = tags[:-1] + ["_"]
    elif mode == 2:
        patterns = tags[:-1]; rng.shuffle(patterns)
    elif mode == 3:
        patterns = tags[:] + [tags[0]]
    elif mode == 4:
        patterns = ["_", tags[0]]
    else:
        patterns = tags[:] + ["_"]  # valid complete cover, wildcard last
    rendered = [p if p == "_" else f"{p}(value)" for p in patterns]
    expected, _, bad = finite_oracle(rendered, universe, "choice")
    diagnostic = None
    if not expected:
        if bad == "_":
            diagnostic = "The '_' catch-all branch must be the last branch"
        elif bad is not None and rendered.index(bad) > 0 and "_" in rendered[:rendered.index(bad)]:
            diagnostic = "Unreachable pick branch after the '_' catch-all branch"
        elif bad is not None and rendered.count(bad) > 1:
            diagnostic = f"Duplicate pick branch: {bad.split(chr(40), 1)[0]}"
        else:
            diagnostic = "Non-exhaustive pick for choice type 'Generated'"
    arms = "\n".join(f"        {p} {{ println({n}) }}" for n, p in enumerate(rendered))
    ctor = f"{tags[0]}(0)"
    source = f"""choice Generated {{\n""" + "\n".join(
        f"    {tag}(value: {'Int32' if payload else 'UInt8'})" for tag in tags
    ) + f"""\n}}
fn main() {{
    val value: Generated = {ctor}
    pick value {{
{arms}
    }}
}}
"""
    first = next(n for n, p in enumerate(rendered) if choice_covered(p, universe) and f"{tags[0]}/payload" in choice_covered(p, universe))
    return Case(f"choice-{i:03d}", source, expected, universe, diagnostic,
                expected and i % 3 == 0, f"{first}\n" if expected and i % 3 == 0 else "")


def tuple_case(i: int, rng: random.Random) -> Case:
    # Include flat and genuinely nested shapes; bindings and '_' are equivalent
    # leaves, while tuple nesting/arity is matched independently.
    shape = "(field, (field, field))" if i % 2 else "((field, field), field)"
    pattern = shape.replace("field", "left", 1)
    pattern = pattern.replace("field", "_", 1).replace("field", "right", 1)
    universe = frozenset({repr(tuple_shape(shape))})
    mode = i % 5
    if mode == 0:
        patterns = [pattern]
    elif mode == 1:
        patterns = ["_",]  # wildcard is valid and irrefutable
    elif mode == 2:
        patterns = [pattern, pattern]
    elif mode == 3:
        patterns = ["_", pattern]
    else:
        patterns = [pattern, "_"]  # tuple pattern is already irrefutable
    expected, _, bad = finite_oracle(patterns, universe, "tuple")
    diagnostic = None
    if not expected:
        if bad is not None and patterns.index(bad) > 0 and "_" in patterns[:patterns.index(bad)]:
            diagnostic = "Unreachable pick branch after the '_' catch-all branch"
        elif bad == "_":
            diagnostic = ("Unreachable pick branch after an irrefutable tuple pattern"
                          if patterns and patterns[0] != "_" else
                          "The '_' catch-all branch must be the last branch")
        else:
            diagnostic = "Unreachable pick branch after an irrefutable tuple pattern"
    arms = "\n".join(f"        {p} {{ println({n}) }}" for n, p in enumerate(patterns))
    value = "(true, (0, 1))" if shape == "(field, (field, field))" else "((0, 1), true)"
    source = f"""fn main() {{
    val value = {value}
    pick value {{
{arms}
    }}
}}
"""
    return Case(f"nested-tuple-{i:03d}", source, expected, universe, diagnostic,
                expected and i % 4 == 0, "0\n" if expected and i % 4 == 0 else "")


def generated_cases() -> list[Case]:
    rng = random.Random(SEED)
    result = ([choice_case(i, rng) for i in range(CASES // 2)] +
              [tuple_case(i, rng) for i in range(CASES // 2)])
    for kind in ("choice", "tuple"):
        for prefix in (False, True):
            universe = frozenset({"a/payload", "b/payload"}) if kind == "choice" else frozenset({"('field', ('field', 'field'))"})
            patterns = (["a(value)" if kind == "choice" else "(first, (second, third))"] if prefix else []) + ["_", "_"]
            expected, _, _ = finite_oracle(patterns, universe, kind)
            assert not expected, "duplicate wildcard must not be accepted by the oracle"
            declaration = "choice Generated {\n    a(value: Int32)\n    b(value: Int32)\n}\n" if kind == "choice" else ""
            value = "val value: Generated = a(0)" if kind == "choice" else "val value = (true, (0, 1))"
            arms = "\n".join(f"        {p} {{ println({n}) }}" for n, p in enumerate(patterns))
            source = declaration + f"fn main() {{\n    {value}\n    pick value {{\n{arms}\n    }}\n}}\n"
            result.append(Case(f"{kind}-duplicate-wildcard-{int(prefix)}", source, False, universe,
                               "Unreachable pick branch after an irrefutable tuple pattern" if kind == "tuple" and prefix
                               else "The '_' catch-all branch must be the last branch"))
    return result


def compiler_path() -> str:
    if len(sys.argv) > 1:
        return str(Path(sys.argv[1]).resolve())
    return str(Path(os.environ.get("CLAW_EXE", Path(__file__).resolve().parents[2] / "build" / "claw")).resolve())


def run(compiler: str, case: Case, root: Path) -> tuple[bool, str]:
    path = root / f"{case.name}.cat"
    path.write_text(case.source, encoding="utf-8")
    env = dict(os.environ, NO_COLOR="1")
    checked = subprocess.run([compiler, "check", str(path)], capture_output=True, text=True, timeout=30, env=env)
    output = (checked.stdout + checked.stderr).replace("\r", "")
    accepted = checked.returncode == 0
    if accepted != case.expected_accept:
        if accepted and case.expected_diagnostic:
            return False, "compiler accepted a case the finite oracle rejects; expected diagnostic was absent: " + case.expected_diagnostic
        return False, f"expected {'accept' if case.expected_accept else 'reject'}, got {'accept' if accepted else 'reject'}\n{output}"
    if not accepted:
        if not case.expected_diagnostic or case.expected_diagnostic not in output:
            return False, "rejected, but expected diagnostic was absent: " + str(case.expected_diagnostic) + "\n" + output
        return True, output
    if case.build:
        exe = root / f"{case.name}.exe"
        built = subprocess.run([compiler, "build", str(path), str(exe)], capture_output=True, text=True, timeout=30, env=env)
        if built.returncode != 0:
            return False, "check accepted but bounded build failed\n" + (built.stdout + built.stderr)
        native = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, env=env)
        actual = native.stdout.replace("\r", "")
        if native.returncode != 0 or actual != case.expected_stdout:
            return False, f"native output mismatch: expected {case.expected_stdout!r}, got {actual!r}\n{native.stderr}"
    return True, output


def main() -> int:
    compiler = compiler_path()
    if not Path(compiler).is_file() or not os.access(compiler, os.X_OK):
        print(f"pattern oracle: missing executable: {compiler}", file=sys.stderr); return 2
    cases = generated_cases()
    with tempfile.TemporaryDirectory(prefix="claw-pattern-oracle-") as td:
        root = Path(td)
        for case in cases:
            ok, detail = run(compiler, case, root)
            if not ok:
                print(f"pattern oracle mismatch: {case.name}", file=sys.stderr)
                print(f"finite universe: {sorted(case.universe)}", file=sys.stderr)
                print("--- minimized source ---", file=sys.stderr)
                print(case.source, file=sys.stderr, end="")
                print("--- detail ---\n" + detail, file=sys.stderr)
                return 1
    accepted = sum(c.expected_accept for c in cases)
    built = sum(c.build and c.expected_accept for c in cases)
    print(f"pattern oracle: {len(cases)} deterministic cases passed (seed={SEED}, accept={accepted}, reject={len(cases)-accepted}, built={built})")
    print("scope: finite independent choice-tag and nested tuple-shape matching; not a general exhaustiveness proof")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
