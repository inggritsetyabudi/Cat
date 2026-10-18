#!/usr/bin/env python3
"""Validate FEATURE-STATUS.md's declarative, bounded contract."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HEADERS = ("ID", "Sumber", "Irisan", "Label", "Keputusan semantik", "Lingkup implementasi", "Bukti penerimaan", "Sisa", "Dimensi non-compiler")
LABELS = ("[diusulkan]", "[diputuskan]", "[diterima compiler]")
SOURCE_RE = re.compile(r"^(?:PRD:\d+|PLAN:(?:G[1-8]|M(?:0[1-9]|10)))$")
PRD_HEADING_RE = re.compile(r"^##\s+(\d+)(?:[.:\s]|$)")
LINK_RE = re.compile(r"!?(?<!\!)\[([^]]+)\]\(([^)]+)\)")
HEADING_RE = re.compile(r"^#{1,6}\s+(.+?)\s*#*\s*$")
# Negated residual language is explicitly bounded, not a completion claim.
NEGATED_UNIVERSAL_RE = re.compile(r"(?i)\b(?:tidak|belum|bukan|never|not|no)\s+(?:diperluas\s+ke\s+)?(?:all|every|everything|complete|completed|fully|universal|semua|seluruh|sepenuhnya|lengkap|tanpa\s+batas|nihil)")
UNIVERSAL_RE = re.compile(r"(?i)\b(?:all|every|everything|complete|completed|fully|universal|zero\s+gaps|100\s*%|semua|seluruh|sepenuhnya|lengkap|tanpa\s+batas|nihil)\b")
NONE_RE = re.compile(r"(?i)^(?:none|n/?a|na|tidak\s+ada|tidak\s+tersisa|nihil|-|0)$")
EXECUTABLE_SUFFIXES = {".cat", ".py", ".sh", ".bat", ".cmd", ".ps1", ".ll"}

class ValidationError(Exception):
    pass

def github_slug(text: str) -> str:
    text = re.sub(r"[`*_~]", "", text).casefold()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    return re.sub(r"[-\s]+", "-", text).strip("-")

def headings(path: Path) -> set[str]:
    return {github_slug(m.group(1)) for m in map(HEADING_RE.match, path.read_text(encoding="utf-8").splitlines()) if m}

def split_row(line: str) -> list[str] | None:
    if not line.lstrip().startswith("|") or not line.rstrip().endswith("|"):
        return None
    return [cell.strip() for cell in line.strip()[1:-1].split("|")]

def is_separator(cells: list[str]) -> bool:
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells)

def local_link_target(root: Path, base: Path, target: str, line: int) -> tuple[Path, str | None]:
    target = target.strip()
    if not target or re.match(r"(?i)^(?:https?:|mailto:|ftp:|//)", target):
        raise ValidationError(f"line {line}: evidence link must be local Markdown")
    target = target.split("?", 1)[0]
    raw_path, anchor = (target.split("#", 1) + [None])[:2] if "#" in target else (target, None)
    anchor = anchor or None
    if not raw_path or raw_path.startswith(("/", "\\")):
        raise ValidationError(f"line {line}: evidence link path is not safe relative")
    rel = Path(raw_path.replace("/", "/"))
    if any(part == "" for part in rel.parts):
        raise ValidationError(f"line {line}: evidence link path is not safe relative: {raw_path}")
    resolved = (base / rel).resolve()
    if root.resolve() not in resolved.parents or not resolved.is_file():
        raise ValidationError(f"line {line}: evidence file does not exist: {raw_path}")
    if anchor and github_slug(anchor) not in headings(resolved):
        raise ValidationError(f"line {line}: evidence anchor not found in {raw_path}: #{anchor}")
    return resolved, anchor

def _bounded_text(text: str) -> str:
    return NEGATED_UNIVERSAL_RE.sub("", text)

def validate(root: Path) -> list[str]:
    doc, prd = root / "cat-northstar/FEATURE-STATUS.md", root / "cat-northstar/PRD.md"
    if not doc.is_file(): return [f"missing status document: {doc.relative_to(root)}"]
    if not prd.is_file(): return [f"missing PRD source: {prd.relative_to(root)}"]
    lines = doc.read_text(encoding="utf-8").replace("\r\n", "\n").splitlines()
    errors: list[str] = []
    header_indexes = [i for i, line in enumerate(lines) if split_row(line) == list(HEADERS)]
    if not header_indexes: return ["missing exact 9-column FEATURE-STATUS table header"]
    if len(header_indexes) > 1: errors.append("duplicate FEATURE-STATUS table headers")
    header_at = header_indexes[0]
    if header_at + 1 >= len(lines): errors.append("table has no separator row")
    else:
        separator = split_row(lines[header_at + 1])
        if separator is None or len(separator) != 9 or not is_separator(separator): errors.append(f"line {header_at + 2}: malformed 9-column table separator")
    rows: list[tuple[int, list[str]]] = []
    for index in range(header_at + 2, len(lines)):
        text = lines[index]
        if not text.strip(): continue
        cells = split_row(text)
        if cells is None:
            # Do not let a malformed G2 row disappear as ordinary prose.
            if re.match(r"\s*G2-[A-Za-z0-9-]+\b", text) or "|" in text:
                errors.append(f"line {index + 1}: malformed table row (missing pipe delimiters)")
            continue
        if is_separator(cells): continue
        rows.append((index + 1, cells))
    if not rows: errors.append("table has no feature rows")
    prd_numbers = {m.group(1) for line in prd.read_text(encoding="utf-8").splitlines() if (m := PRD_HEADING_RE.match(line))}
    required_sources = {f"PRD:{n}" for n in prd_numbers} | {f"PLAN:G{i}" for i in range(1, 9)} | {f"PLAN:M{i:02d}" for i in range(1, 11)}
    ids: set[str] = set(); sources: list[str] = []
    for line, cells in rows:
        if len(cells) != 9:
            errors.append(f"line {line}: expected exactly 9 cells, found {len(cells)}"); continue
        if any(not c for c in cells): errors.append(f"line {line}: every table cell must be nonempty")
        feature_id, source, _, label = cells[:4]
        if not re.fullmatch(r"G2-[A-Z0-9-]+", feature_id): errors.append(f"line {line}: invalid ID: {feature_id}")
        elif feature_id in ids: errors.append(f"line {line}: duplicate ID: {feature_id}")
        ids.add(feature_id)
        if not SOURCE_RE.fullmatch(source): errors.append(f"line {line}: invalid source: {source}")
        sources.append(source)
        if label not in LABELS: errors.append(f"line {line}: label must be exactly one of {LABELS}: {label}")
        if sum(" | ".join(cells).count(candidate) for candidate in LABELS) != 1: errors.append(f"line {line}: labels may occur only once, in the Label cell")
        evidence = cells[6]; links = list(LINK_RE.finditer(evidence)); resolved_links: list[Path] = []
        if not links: errors.append(f"line {line}: Bukti penerimaan needs a local Markdown link")
        for link in links:
            try: resolved, _ = local_link_target(root, doc.parent, link.group(2), line); resolved_links.append(resolved)
            except ValidationError as exc: errors.append(str(exc))
        if label == "[diterima compiler]":
            if not cells[5].startswith("Teruji:"): errors.append(f"line {line}: accepted implementation scope must start with 'Teruji:'")
            if NONE_RE.fullmatch(cells[7].strip()): errors.append(f"line {line}: accepted row must document a non-empty Sisa limitation")
            # Residuals describe what is NOT accepted; broad vocabulary there is
            # not a positive implementation claim. Semantic intent is separate.
            if UNIVERSAL_RE.search(_bounded_text(cells[5])): errors.append(f"line {line}: broad/unbounded completion claim is not allowed")
            executable = [p for p in resolved_links if root / "claw" in p.parents and any(part in {"tests", "test"} for part in p.relative_to(root / "claw").parts) and p.suffix.casefold() in EXECUTABLE_SUFFIXES]
            changelog = [p for p in resolved_links if "changelog" in p.relative_to(root).parts]
            if changelog and not executable and not re.search(r"(?i)\b(?:bounded|scope|limitation|limited|terbatas|batasan)\b", evidence): errors.append(f"line {line}: changelog acceptance evidence must state a bounded scope or limitation")
            if not executable and not changelog: errors.append(f"line {line}: accepted evidence must reference an existing executable fixture under claw/tests or claw/test, or a bounded changelog")
    missing = sorted(required_sources - set(sources), key=lambda s: (s.split(":", 1)[0], s))
    if missing: errors.append("missing required sources: " + ", ".join(missing))
    unknown = sorted(set(sources) - required_sources)
    if unknown: errors.append("unrecognized sources: " + ", ".join(unknown))
    return errors

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3]); args = parser.parse_args(argv)
    errors = validate(args.root.resolve())
    if errors:
        for error in errors: print(f"FAIL: {error}", file=sys.stderr)
        print(f"FEATURE-STATUS: FAIL ({len(errors)} issue(s))"); return 1
    print("FEATURE-STATUS: PASS"); return 0

if __name__ == "__main__": raise SystemExit(main())
