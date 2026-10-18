#!/usr/bin/env python3
"""Validate FEATURE-STATUS.md's declarative, bounded contract."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from feature_status_shell_checks import STDOUT_IF, stdout_check_errors
from feature_status_source_index import validate_source_index
from feature_status_roadmap import validate_roadmap_inventory

HEADERS = ("ID", "Sumber", "Irisan", "Label", "Keputusan semantik", "Lingkup implementasi", "Bukti penerimaan", "Sisa", "Dimensi non-compiler")
LABELS = ("[diusulkan]", "[diputuskan]", "[diterima compiler]")
SOURCE_RE = re.compile(r"^(?:PRD:\d+|PLAN:(?:G[1-8]|M(?:0[1-9]|10)|S\d+)|ROADMAP:\d+)$")
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

def heading_numbers(path: Path) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = HEADING_RE.match(line)
        if match:
            result.setdefault(github_slug(match.group(1)), set()).update(re.findall(r"(?<!\d)(\d+)(?!\d)", match.group(1)))
    return result

def _manifest_source(root: Path, source: str, anchor: str, line: int) -> str | None:
    if not source or not anchor or not source.startswith("cat-northstar/"):
        raise ValidationError(f"line {line}: source_anchor must be a relative cat-northstar path with #heading-slug")
    raw_path, actual_anchor = source.split("#", 1) if "#" in source else (source, "")
    if actual_anchor != anchor:
        raise ValidationError(f"line {line}: source_anchor has inconsistent anchor")
    path = (root / raw_path).resolve()
    if root.resolve() not in path.parents or not path.is_file():
        raise ValidationError(f"line {line}: source_anchor file does not exist: {raw_path}")
    if actual_anchor not in headings(path):
        raise ValidationError(f"line {line}: source_anchor heading not found: {source}")
    return actual_anchor

def _source_number(source_cell: str) -> str:
    value = source_cell.split(":", 1)[1]
    value = value[1:] if value[:1] in {"G", "M", "S"} else value
    return str(int(value))

def _source_line_in_heading(path: Path, anchor: str, line_number: int) -> bool:
    lines = path.read_text(encoding="utf-8").splitlines()
    starts = [i for i, raw in enumerate(lines) if (m := HEADING_RE.match(raw)) and github_slug(m.group(1)) == anchor]
    if not starts or line_number < 1:
        return False
    start = starts[0]
    heading = HEADING_RE.match(lines[start])
    level = len(lines[start]) - len(lines[start].lstrip("#")) if heading else 1
    end = next((i for i in range(start + 1, len(lines))
                if (m := HEADING_RE.match(lines[i])) and len(lines[i]) - len(lines[i].lstrip("#")) <= level), len(lines))
    return start < line_number <= end

def _source_anchor_ok(root: Path, source_cell: str, source_anchor: str, line: int, atom_id: str | None = None) -> None:
    if not SOURCE_RE.fullmatch(source_cell):
        raise ValidationError(f"line {line}: invalid source: {source_cell}")
    if "#" not in source_anchor:
        raise ValidationError(f"line {line}: source_anchor must include a heading anchor")
    raw_path, anchor = source_anchor.split("#", 1)
    _manifest_source(root, raw_path + "#" + anchor, anchor, line)
    path = (root / raw_path).resolve()
    if source_cell.startswith("ROADMAP:"):
        expected = source_cell.split(":", 1)[1]
        roadmap = (root / "cat-northstar/ROADMAP-v1-plus.md").resolve()
        if path != roadmap:
            raise ValidationError(f"line {line}: {source_cell} source_anchor must target ROADMAP-v1-plus.md")
        if not anchor.startswith(expected + "-"):
            raise ValidationError(f"line {line}: source_anchor heading number does not match {source_cell}: {source_anchor}")
        return
    if source_cell.startswith("PLAN:S"):
        source_line = int(source_cell.split("S", 1)[1])
        if path.name != "PRD-CHANGES-v0.1-to-v1.0.md":
            raise ValidationError(f"line {line}: {source_cell} source_anchor must target PRD-CHANGES-v0.1-to-v1.0.md")
        if not _source_line_in_heading(path, anchor, source_line):
            raise ValidationError(f"line {line}: {source_cell} source line is not enclosed by {source_anchor}")
        return
    # These two atoms keep M02 as staging, but their normative rule is §3/S76.
    # Do not extend this exception to the milestone itself or other PLAN rows.
    if atom_id in {"G2-PLAN-M02-IMPLICIT-TAIL", "G2-PLAN-M02-IMPLICIT-WARNING"}:
        normative_anchor = "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#3-perubahan-per-bagian-prd"
        if source_cell != "PLAN:M02" or source_anchor != normative_anchor:
            raise ValidationError(f"line {line}: {atom_id} requires PLAN:M02 staging and normative §3 implicit-return source")
        lines = path.read_text(encoding="utf-8").splitlines()
        start = next(i for i, raw in enumerate(lines) if (m := HEADING_RE.match(raw)) and github_slug(m.group(1)) == anchor)
        end = next((i for i in range(start + 1, len(lines)) if HEADING_RE.match(lines[i])), len(lines))
        requirement = "| 33 | Implicit return | Hanya ekspresi terakhir blok | D v0.1, K v0.2 | ekspresi di tengah blok → peringatan |"
        if requirement not in lines[start:end]:
            raise ValidationError(f"line {line}: {atom_id} exact normative implicit-return row not found in §3")
        return
    numbers = heading_numbers(path).get(anchor, set())
    expected = _source_number(source_cell)
    if not source_cell.startswith("PLAN:") and (path != (root / "cat-northstar/PRD.md").resolve() or not anchor.startswith(expected + "-")):
        raise ValidationError(f"line {line}: source_anchor heading number does not match {source_cell}: {source_anchor}")
    if source_cell.startswith("PLAN:"):
        if path.name != "PRD-CHANGES-v0.1-to-v1.0.md":
            raise ValidationError(f"line {line}: {source_cell} source_anchor must target PRD-CHANGES-v0.1-to-v1.0.md")
        plan = source_cell.split(":", 1)[1]
        kind, number = plan[0], int(plan[1:])
        expected = "2" if kind == "G" else "4"
        if not anchor.startswith(expected + "-"):
            raise ValidationError(f"line {line}: {source_cell} source_anchor must be the §{expected} heading")
        token = f"G{number}" if kind == "G" else ("v1.0" if number == 10 else f"v0.{number}")
        lines = path.read_text(encoding="utf-8").splitlines()
        start = next((i for i, raw in enumerate(lines) if (m := HEADING_RE.match(raw)) and github_slug(m.group(1)) == anchor), None)
        end = next((i for i in range(start + 1, len(lines)) if HEADING_RE.match(lines[i])), len(lines)) if start is not None else 0
        found = any(token in [re.sub(r"[*`]", "", c.strip()) for c in raw.strip().strip("|").split("|")] for raw in lines[start:end] if raw.lstrip().startswith("|"))
        if not found:
            raise ValidationError(f"line {line}: {source_cell} exact table row token not found in {source_anchor}")

def _bounded_block(lines: list[str], line_no: int) -> tuple[int, int]:
    """Return a conservative function-like block; None-like whole-file is intentional."""
    starts = [i for i, line in enumerate(lines) if re.match(r"\s*(?:[A-Za-z_][\w-]*\s*\(.*\)\s*\{|def\s+\w+|function\s+\w+|[A-Za-z_]\w*\s*\(\)\s*\{)", line)]
    prior = max((i for i in starts if i <= line_no), default=0)
    following = min((i for i in starts if i > line_no), default=len(lines))
    return prior, following

def _fixture_assertion_matches(lines: list[str], token: str, assertion: str) -> bool:
    """Pair shell assertions with the nearest compiler/test invocation.

    This is a bounded lexical check, not a shell interpreter or semantic proof.
    Continued commands and simple fixture-path aliases cover the repo runners.
    """
    text = "\n".join(lines)
    positions = [text.count("\n", 0, match.start()) for match in re.finditer(re.escape(assertion), text)]
    aliases = {match.group(1) for line in lines if token in line
               if (match := re.match(r"\s*(\w+)=", line))}
    commands = []
    index = 0
    while index < len(lines):
        start = index
        command = lines[index]
        while command.rstrip().endswith("\\") and index + 1 < len(lines):
            index += 1
            command += "\n" + lines[index]
        if re.search(r'"\$CLAW_EXE"\s+(?:build|check|air|oir|llvm|validate)\b|^\s*run_(?:check|air|oir|validate)_(?:pass|fail)\b', command):
            related = token in command or any(re.search(r"\$\{?" + re.escape(alias) + r"(?:\}|\b)", command) for alias in aliases)
            commands.append((start, index, related))
        index += 1
    for position in positions:
        preceding = [command for command in commands if command[0] <= position]
        if preceding:
            if preceding[-1][2]: return True
        elif not commands:
            # Minimal non-shell fixtures retain the conservative function check.
            fixture_positions = [i for i, value in enumerate(lines) if token in value]
            if any(_bounded_block(lines, position) == _bounded_block(lines, f) for f in fixture_positions): return True
    return False

def _validate_manifest(root: Path, rows: list[tuple[int, list[str]]], errors: list[str]) -> None:
    path = root / "cat-northstar/feature-status-atoms.json"
    if not path.is_file():
        errors.append(f"missing feature atom manifest: {path.relative_to(root)}")
        return
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"invalid feature atom manifest: {exc}"); return
    if not isinstance(manifest, dict):
        errors.append("feature atom manifest must be an object keyed by ID"); return
    errors.extend(validate_source_index(root, rows, manifest))
    row_map = {cells[0]: (line, cells) for line, cells in rows if len(cells) == 9}
    ids, accepted = set(row_map), {key for key, (_, cells) in row_map.items() if cells[3] == "[diterima compiler]"}
    missing, extra = sorted(ids - set(manifest)), sorted(set(manifest) - ids)
    if missing: errors.append("manifest missing atom(s): " + ", ".join(missing))
    if extra: errors.append("manifest has unknown atom(s): " + ", ".join(extra))
    for key in sorted(ids & set(manifest)):
        line, cells = row_map[key]; atom = manifest[key]
        if not isinstance(atom, dict): errors.append(f"line {line}: manifest atom {key} must be an object"); continue
        required = {"source_anchor", "fixtures", "assertions"}
        if set(atom) != required: errors.append(f"line {line}: manifest atom {key} must contain exactly source_anchor, fixtures, assertions")
        source_anchor = atom.get("source_anchor")
        if not isinstance(source_anchor, str): errors.append(f"line {line}: manifest atom {key} source_anchor must be a string")
        else:
            try: _source_anchor_ok(root, cells[1], source_anchor, line, key)
            except ValidationError as exc: errors.append(str(exc))
        if cells[1].startswith("PLAN:S") and cells[3] == "[diterima compiler]":
            errors.append(f"line {line}: PLAN:S sources are documentation-only and cannot be accepted by the compiler")
        fixtures = atom.get("fixtures")
        assertions = atom.get("assertions")
        fixtures_ok = isinstance(fixtures, list) and all(isinstance(x, str) and x for x in fixtures)
        assertions_ok = isinstance(assertions, list) and all(isinstance(x, dict) and set(x) in ({"path", "text"}, {"path", "text", "checks"}) and isinstance(x.get("checks", []), list) and all(isinstance(check, str) and check for check in x.get("checks", [])) and isinstance(x.get("path"), str) and x.get("path") and isinstance(x.get("text"), str) and x.get("text") for x in assertions)
        if not fixtures_ok: errors.append(f"line {line}: manifest atom {key} fixtures must be an array of string paths")
        if not assertions_ok: errors.append(f"line {line}: manifest atom {key} assertions must be path/text objects")
        evidence_links = set()
        for link in LINK_RE.finditer(cells[6]):
            try: evidence_links.add(local_link_target(root, root / "cat-northstar", link.group(2), line))
            except ValidationError: pass
        if isinstance(source_anchor, str) and "#" in source_anchor:
            raw_path, anchor = source_anchor.split("#", 1)
            if ((root / raw_path).resolve(), anchor) not in evidence_links:
                errors.append(f"line {line}: source_anchor is not linked by ledger evidence")
        if cells[1].startswith("PLAN:S") and SOURCE_RE.fullmatch(cells[1]):
            index_path = root / "cat-northstar/feature-status-source-index.json"
            try:
                index_atoms = json.loads(index_path.read_text(encoding="utf-8")).get("atoms", [])
            except (OSError, json.JSONDecodeError, AttributeError):
                index_atoms = []
            indexed = next((item for item in index_atoms if isinstance(item, dict) and item.get("id") == key), None)
            if indexed is not None:
                source_line = int(cells[1].split("S", 1)[1])
                source_file = root / "cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md"
                source_lines = source_file.read_text(encoding="utf-8").splitlines() if source_file.is_file() else []
                excerpt = indexed.get("source_excerpt")
                if source_line < 1 or source_line > len(source_lines):
                    errors.append(f"line {line}: {cells[1]} source line does not exist in PRD-CHANGES-v0.1-to-v1.0.md")
                elif not isinstance(excerpt, str) or excerpt not in source_lines[source_line - 1]:
                    errors.append(f"line {line}: {cells[1]} source_excerpt must be a substring of source line {source_line}")
        if not fixtures_ok or not assertions_ok: continue
        if key not in accepted: continue
        if not fixtures: errors.append(f"line {line}: accepted manifest atom {key} fixtures must be nonempty")
        if not assertions: errors.append(f"line {line}: accepted manifest atom {key} assertions must be nonempty")
        evidence_paths = set()
        for link in LINK_RE.finditer(cells[6]):
            try: evidence_paths.add(local_link_target(root, root / "cat-northstar", link.group(2), line)[0])
            except ValidationError: pass
        for fixture in fixtures if isinstance(fixtures, list) else []:
            target = (root / fixture).resolve()
            if root.resolve() not in target.parents or not target.is_file(): errors.append(f"line {line}: fixture does not exist: {fixture}")
            if target not in evidence_paths: errors.append(f"line {line}: fixture is not linked by ledger evidence: {fixture}")
        for assertion in assertions if isinstance(assertions, list) else []:
            runner_path = assertion.get("path", "") if isinstance(assertion, dict) else ""
            runner = (root / runner_path).resolve(); text = assertion.get("text", "") if isinstance(assertion, dict) else ""
            if root.resolve() not in runner.parents or not runner.is_file(): errors.append(f"line {line}: assertion runner does not exist: {runner_path}"); continue
            if runner not in evidence_paths: errors.append(f"line {line}: assertion runner is not linked by ledger evidence: {runner_path}")
            runner_lines = runner.read_text(encoding="utf-8").splitlines()
            if runner.suffix == ".sh":
                if re.match(r"^\s*\w+=", text):
                    errors.append(f"line {line}: a variable declaration is not acceptance assertion evidence")
                checks = assertion.get("checks", [])
                if checks and not STDOUT_IF.fullmatch(text.strip()):
                    errors.append(f"line {line}: companion checks require a primary stdout comparison")
                for check in ([text] if STDOUT_IF.fullmatch(text.strip()) else []) + checks:
                    for issue in stdout_check_errors(runner_lines, check):
                        errors.append(f"line {line}: {runner_path}: {issue}: {check!r}")
            elif assertion.get("checks"):
                errors.append(f"line {line}: companion checks require a shell runner")
            if text not in runner.read_text(encoding="utf-8"): errors.append(f"line {line}: assertion text not found in runner: {text!r}")
            tokens = [Path(f).parent.name if Path(f).name == "main.cat" else Path(f).name for f in fixtures]
            if text and not any(_fixture_assertion_matches(runner_lines, token, text) for token in tokens):
                errors.append(f"line {line}: assertion has no fixture in the same bounded runner block")
        # Different fixtures may be checked by different runners (frontend/native).
        # Workspace entrypoints are passed by directory, not by `main.cat`.
        for fixture in fixtures:
            fixture_path = Path(fixture)
            token = fixture_path.parent.name if fixture_path.name == "main.cat" else fixture_path.name
            matched = False
            for assertion in assertions:
                runner = (root / assertion["path"]).resolve()
                if root.resolve() not in runner.parents or not runner.is_file(): continue
                runner_lines = runner.read_text(encoding="utf-8").splitlines()
                if _fixture_assertion_matches(runner_lines, token, assertion["text"]): matched = True
            if not matched: errors.append(f"line {line}: fixture basename is not referenced by runner in the same bounded runner block: {token}")

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
        if source.startswith("PLAN:S") and label == "[diterima compiler]": errors.append(f"line {line}: PLAN:S sources are documentation-only and cannot be accepted by the compiler")
        if source.startswith("ROADMAP:") and label != "[diusulkan]": errors.append(f"line {line}: ROADMAP sources are design proposals and must remain [diusulkan]")
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
    # PLAN:S<n> is an optional, line-addressed documentation/governance atom;
    # unlike PRD/G/M coverage, adding one does not make every S line mandatory.
    unknown = sorted(source for source in set(sources) - required_sources if not source.startswith(("PLAN:S", "ROADMAP:")))
    if unknown: errors.append("unrecognized sources: " + ", ".join(unknown))
    _validate_manifest(root, rows, errors)
    # Full PRD inventory: require independently enumerated roadmap owners even
    # when an atom has been removed consistently from ledger/index/manifest.
    # Small synthetic unit-test PRDs do not represent the full product corpus.
    if "20" in prd_numbers:
        errors.extend(validate_roadmap_inventory(root, rows))
    return errors

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3]); args = parser.parse_args(argv)
    errors = validate(args.root.resolve())
    if errors:
        for error in errors: print(f"FAIL: {error}", file=sys.stderr)
        print(f"FEATURE-STATUS: FAIL ({len(errors)} issue(s))"); return 1
    print("FEATURE-STATUS: PASS"); return 0

if __name__ == "__main__": raise SystemExit(main())
