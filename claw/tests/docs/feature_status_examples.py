#!/usr/bin/env python3
"""Bounded lexical checks for active Cat examples in approved documentation.

This is intentionally not a Cat parser: it checks Markdown ``cat`` and
``cat proposal`` fences in the explicit active-document inventory. Comments
and string/character literals are blanked while preserving offsets, so prose,
history, and embedded diagnostics are not scanned.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

FENCE_OPEN = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
FENCE_CLOSE = re.compile(r"^ {0,3}(`+|~+)[ \t]*$")
IDENT = re.compile(r"[A-Za-z_]\w*")
EXEMPT_VARIANTS = {"Ok", "Fail", "Some", "None"}
SCALAR_NAMES = {
    "Bool", "Char", "Unit", "Int8", "Int16", "Int32", "Int64", "Int128",
    "UInt8", "UInt16", "UInt32", "UInt64", "UInt128", "USize",
    "Float32", "Float64", "Float128", "Int", "UInt", "Float",
}
# Deliberately explicit: current root Markdown (apart from AGENTS.md), docs,
# claw/docs, and selected CatNorthstar sources are executable-example sources.
# Governance ledgers, plans, changelogs, legacy, and ideas remain out of scope.
ACTIVE_DOCS = (
    "ARCHITECTURE.md", "CONTRIBUTING.md", "README.md", "SPEC.md",
    "docs/BUILDING.md", "docs/CLI.md", "docs/README.md", "docs/STATUS.md",
    "docs/TESTING.md",
    "claw/README.md", "cat-northstar/PRD.md", "cat-northstar/ROADMAP-v1-plus.md",
    "cat-northstar/README.md", "cat-northstar/GLOBAL-EXAMPLES.md",
    "claw/docs/README.md", "claw/docs/builtin-methods.md",
    "claw/docs/frontend-safety-status.md", "claw/docs/roadmap-next.md",
)

@dataclass(frozen=True)
class Fence:
    path: Path
    line: int
    info: str
    body: str

@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    rule: str
    message: str

@dataclass
class ScanReport:
    findings: list[Finding]
    inventory: list[str]
    excluded: dict[str, list[str]]
    scanned: dict[str, list[str]]


def read_fences(path: Path) -> Iterable[Fence]:
    """Yield Markdown fences, including tilde fences and multiline bodies."""
    lines = path.read_text(encoding="utf-8").replace("\r\n", "\n").splitlines()
    i = 0
    while i < len(lines):
        opening = FENCE_OPEN.match(lines[i])
        if not opening:
            i += 1
            continue
        marker, info = opening.group(1), opening.group(2).strip()
        start = i + 1
        i += 1
        body: list[str] = []
        while i < len(lines):
            closing = FENCE_CLOSE.match(lines[i])
            if closing and closing.group(1)[0] == marker[0] and len(closing.group(1)) >= len(marker):
                yield Fence(path, start, info, "\n".join(body) + "\n")
                break
            body.append(lines[i])
            i += 1
        else:
            raise ValueError(f"unclosed Markdown fence at {path}:{start}")
        i += 1


def strip_comments_and_strings(source: str) -> str:
    """Blank comments and quoted literals while retaining newlines/positions."""
    out = list(source)
    i, n = 0, len(source)
    state = "code"
    while i < n:
        c = source[i]
        if state == "code":
            if source.startswith("//", i):
                out[i:i + 2] = "  "; i += 2; state = "line"; continue
            if source.startswith("/*", i):
                out[i:i + 2] = "  "; i += 2; state = "block"; continue
            if c in ('"', "'", "`"):
                out[i] = " "; i += 1; state = c; continue
            i += 1; continue
        if state == "line":
            if c == "\n": state = "code"
            else: out[i] = " "
            i += 1; continue
        if state == "block":
            if source.startswith("*/", i):
                out[i:i + 2] = "  "; i += 2; state = "code"
            else:
                if c != "\n": out[i] = " "
                i += 1
            continue
        # quoted literal; escapes are blanked as a unit, including escaped quote
        if c == "\\" and i + 1 < n:
            if source[i] != "\n": out[i] = " "
            if source[i + 1] != "\n": out[i + 1] = " "
            i += 2; continue
        if c == state:
            out[i] = " "; i += 1; state = "code"; continue
        if c != "\n": out[i] = " "
        i += 1
    return "".join(out)


def _line_at(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def _finding(fence: Fence, body: str, offset: int, rule: str, message: str) -> Finding:
    return Finding(fence.path, fence.line + _line_at(body, offset), rule, message)


def _matching(text: str, start: int, left: str = "(", right: str = ")") -> int | None:
    depth = 0
    for i in range(start, len(text)):
        if text[i] == left: depth += 1
        elif text[i] == right:
            depth -= 1
            if depth == 0: return i
    return None


def _check_g3(fence: Fence, clean: str) -> list[Finding]:
    result: list[Finding] = []
    for match in re.finditer(r"\b([A-Z][A-Za-z0-9_]*)\s*\(", clean):
        name = match.group(1)
        if name in EXEMPT_VARIANTS:
            continue
        before = clean[:match.start()].rstrip()
        if before.endswith(".") or re.search(r"\bfn$", before):
            continue
        close = _matching(clean, clean.find("(", match.start()))
        if close is not None and ":" in clean[match.end():close]:
            result.append(_finding(fence, clean, match.start(), "G3",
                                   f"named-argument constructor {name}(field: ...) is forbidden; use {name}.new(...)"))
    return result


def _check_g4(fence: Fence, clean: str) -> list[Finding]:
    result: list[Finding] = []
    # Declaration names; type declarations themselves and generic parameters are excluded.
    patterns = [
        r"\b(?:val|var)\s+([A-Za-z_]\w*)",
        r"\bfn\s+([A-Za-z_]\w*)\s*(?:\[|\(|->)",
        r"\bmodule\s+([A-Za-z_]\w*)",
    ]
    for pattern in patterns:
        for m in re.finditer(pattern, clean):
            name = m.group(1)
            if name[:1].isupper():
                result.append(_finding(fence, clean, m.start(1), "G4", f"user name {name!r} must start lowercase"))
    # Only a ref parameter at the start of a comma/parenthesis-delimited
    # parameter slot is a value declaration. Do not treat `Maybe[ref Node]`
    # (or other type positions) as a declaration; this bounded lexical check
    # intentionally does not attempt full Cat type/AST parsing.
    for fn in re.finditer(r"\bfn\s+[A-Za-z_]\w*(?:\s*\[[^\]]*\])?\s*\(", clean):
        opening = clean.find("(", fn.start())
        closing = _matching(clean, opening)
        if closing is None:
            continue
        params = clean[opening + 1:closing]
        for m in re.finditer(r"(?:^|,)\s*ref\s+(?:mut\s+)?([A-Za-z_]\w*)", params):
            name = m.group(1)
            if name[:1].isupper() and name not in SCALAR_NAMES:
                result.append(_finding(fence, clean, opening + 1 + m.start(1), "G4", f"user name {name!r} must start lowercase"))
    # Field declarations and parameter declarations (not type positions).
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*:", clean):
        name = m.group(1)
        prefix = clean[max(0, m.start() - 30):m.start()]
        if name[:1].isupper() and not re.search(r"(?:val|var|ref|fn|shape|choice|contract)\s*$", prefix):
            result.append(_finding(fence, clean, m.start(1), "G4", f"field/parameter {name!r} must start lowercase"))
    # Choice variant declarations, including comma-separated and multiline forms.
    for cm in re.finditer(r"\bchoice\s+[A-Za-z_]\w*\s*\{", clean):
        close = _matching(clean, clean.find("{", cm.start()), "{", "}")
        if close is None: continue
        body = clean[clean.find("{", cm.start()) + 1:close]
        for vm in re.finditer(r"(?<![.\w])([A-Za-z_]\w*)\s*(?=\(|,|$)", body, re.M):
            name = vm.group(1)
            # Only depth-zero names are variants; payload types such as
            # Float64/Map occur at parenthesis depth one or more.
            depth = body[:vm.start(1)].count("(") - body[:vm.start(1)].count(")")
            if depth == 0 and name[:1].isupper() and name not in EXEMPT_VARIANTS:
                result.append(_finding(fence, clean, clean.find("{", cm.start()) + 1 + vm.start(1), "G4", f"choice variant {name!r} must start lowercase"))
    return result


def _check_g8(fence: Fence, clean: str) -> list[Finding]:
    findings = [_finding(fence, clean, m.start(), "G8", f"legacy keyword {m.group(0)!r} is forbidden in active Cat examples")
                for m in re.finditer(r"\b(?:pub|realm)\b", clean)]
    # Match the exact legacy module path, not modules.cater or modules.cat2.
    # Strings and comments have already been blanked by the caller.
    findings.extend(_finding(fence, clean, m.start(), "G8", "legacy module path 'modules.cat' is forbidden in active Cat examples")
                    for m in re.finditer(r"(?<!\w)modules\.cat(?!\w)", clean))
    return findings


def scan_fence(fence: Fence) -> list[Finding]:
    clean = strip_comments_and_strings(fence.body)
    findings = _check_g3(fence, clean) + _check_g4(fence, clean) + _check_g8(fence, clean)
    for m in re.finditer(r"\bOption\b", clean):
        findings.append(_finding(fence, clean, m.start(), "G5", "legacy type identifier Option is forbidden; use Maybe"))
    return sorted(findings, key=lambda f: (f.line, f.rule, f.message))


def scan_universal_tokens(fence: Fence) -> list[Finding]:
    """Check rules that apply even to examples marked as expected errors."""
    clean = strip_comments_and_strings(fence.body)
    findings = _check_g8(fence, clean)
    for m in re.finditer(r"\bOption\b", clean):
        findings.append(_finding(fence, clean, m.start(), "G5", "legacy type identifier Option is forbidden; use Maybe"))
    return sorted(findings, key=lambda f: (f.line, f.rule, f.message))


def scan_repository(root: Path) -> ScanReport:
    """Scan only the declared active inventory; return findings and exclusions."""
    root = root.resolve()
    paths = [root / relative for relative in ACTIVE_DOCS]
    findings: list[Finding] = []
    inventory: list[str] = []
    excluded: dict[str, list[str]] = {"expected-error": [], "non-Cat": [], "other": []}
    scanned: dict[str, list[str]] = {"cat": [], "cat proposal": []}
    for path in paths:
        if not path.is_file(): continue
        inventory.append(path.relative_to(root).as_posix())
        for fence in read_fences(path):
            if fence.info in {"cat", "cat proposal"}:
                scanned[fence.info].append(f"{path.relative_to(root)}:{fence.line}")
                findings.extend(scan_fence(fence))
            elif fence.info.startswith("cat error:"):
                excluded["expected-error"].append(f"{path.relative_to(root)}:{fence.line}")
                findings.extend(scan_universal_tokens(fence))
            elif fence.info:
                excluded["non-Cat"].append(f"{path.relative_to(root)}:{fence.line} ({fence.info})")
            else:
                excluded["other"].append(f"{path.relative_to(root)}:{fence.line} (untagged)")
    return ScanReport(findings, inventory, excluded, scanned)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=None, help="repository root (default: this checkout)")
    args = parser.parse_args(argv)
    root = args.root if args.root is not None else Path(__file__).resolve().parents[3]
    try:
        report = scan_repository(root)
    except (OSError, ValueError) as exc:
        print(f"feature-status examples: setup failed: {exc}", file=sys.stderr)
        return 2
    print("Active example inventory: " + ", ".join(report.inventory))
    print("Scanned categories: " + ", ".join(f"{key}={len(value)}" for key, value in report.scanned.items()))
    counts = ", ".join(f"{key}={len(value)}" for key, value in report.excluded.items())
    print("Excluded categories: " + counts + ". Expected-error fences are excluded from G3/G4 declaration checks, but universal legacy-token checks still apply; text/shell/other fences and legacy/history files are out of scope.")
    if report.findings:
        for f in report.findings:
            print(f"FAIL: {f.path}:{f.line} [{f.rule}] {f.message}", file=sys.stderr)
        return 1
    print("feature-status examples: PASS (bounded lexical scan; not a semantic/module-locality or exhaustive casing parser)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
