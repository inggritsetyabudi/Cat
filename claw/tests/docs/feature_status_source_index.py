#!/usr/bin/env python3
"""Validate the manually reviewed authoritative G2 source crosswalk."""
from __future__ import annotations
import json
import re
from pathlib import Path
from typing import Any

_INDEX_RELATIVE = Path("cat-northstar/feature-status-source-index.json")
_TOP_KEYS = {"schema_version", "authority", "maintenance", "boundaries", "atoms"}
_ATOM_KEYS = {"id", "source", "source_anchor", "requirement_description", "source_excerpt"}
_ID_RE = re.compile(r"^G2-[A-Z0-9-]+$")

def _error(message: str) -> str:
    return f"source index: {message}"


def _heading_slug(title: str) -> str:
    title = re.sub(r"[`*_✅🟦]", "", title.lower())
    title = re.sub(r"[^a-z0-9\s-]", "", title)
    return re.sub(r"\s+", "-", title).strip("-")


def _anchored_section(root: Path, anchor: str) -> tuple[str | None, str | None]:
    """Return the independently resolved source section, or a diagnostic."""
    if not isinstance(anchor, str) or anchor.count("#") != 1:
        return None, "source_anchor must be a relative path#heading-slug"
    relative, slug = anchor.split("#", 1)
    source_path = (root / relative).resolve()
    try:
        source_path.relative_to(root.resolve())
    except ValueError:
        return None, "source_anchor path escapes repository root"
    if not slug or not source_path.is_file():
        return None, f"source_anchor target does not exist: {anchor}"
    try:
        lines = source_path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as exc:
        return None, f"cannot read source_anchor target {anchor}: {exc}"
    headings = []
    for index, line in enumerate(lines):
        if line.startswith("#") and _heading_slug(re.sub(r"^#+\s*", "", line)) == slug:
            headings.append(index)
    if not headings:
        return None, f"source_anchor heading not found: {anchor}"
    start = headings[0]
    level = len(lines[start]) - len(lines[start].lstrip("#"))
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if lines[index].startswith("#") and len(lines[index]) - len(lines[index].lstrip("#")) <= level:
            end = index
            break
    return "\n".join(lines[start:end]), None

def validate_source_index(root: Path, rows: list[tuple[int, list[str]]], manifest: dict[str, Any]) -> list[str]:
    """Return diagnostics; the index is static and is never rebuilt at validation time."""
    errors: list[str] = []
    path = root / _INDEX_RELATIVE
    if not path.is_file():
        return [_error(f"missing authoritative source index: {_INDEX_RELATIVE}")]
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [_error(f"invalid authoritative source index: {exc}")]
    if not isinstance(payload, dict):
        return [_error("top level must be an object")]
    if set(payload) != _TOP_KEYS:
        errors.append(_error(f"top-level keys must be exactly {sorted(_TOP_KEYS)}"))
    if payload.get("schema_version") != 1:
        errors.append(_error("schema_version must be 1"))
    for key in ("authority", "maintenance", "boundaries"):
        if not isinstance(payload.get(key), str) or not payload[key].strip():
            errors.append(_error(f"{key} must be a non-empty string"))
    atoms = payload.get("atoms")
    if not isinstance(atoms, list):
        errors.append(_error("atoms must be an array"))
        return errors
    index_map: dict[str, tuple[int, dict[str, Any]]] = {}
    for position, atom in enumerate(atoms, 1):
        if not isinstance(atom, dict):
            errors.append(_error(f"atoms[{position}] must be an object")); continue
        if set(atom) != _ATOM_KEYS:
            errors.append(_error(f"atoms[{position}] keys must be exactly {sorted(_ATOM_KEYS)}"))
        ident = atom.get("id")
        if not isinstance(ident, str) or not _ID_RE.fullmatch(ident):
            errors.append(_error(f"atoms[{position}] has invalid id: {ident!r}")); continue
        if ident in index_map:
            errors.append(_error(f"duplicate source-index ID: {ident}")); continue
        index_map[ident] = (position, atom)
        for field in ("source", "source_anchor", "requirement_description", "source_excerpt"):
            if not isinstance(atom.get(field), str) or not atom[field].strip():
                errors.append(_error(f"atoms[{position}] {field} must be a non-empty string"))
        if isinstance(atom.get("source_anchor"), str) and isinstance(atom.get("source_excerpt"), str):
            section, problem = _anchored_section(root, atom["source_anchor"])
            if problem:
                errors.append(_error(f"atoms[{position}] {ident}: {problem}"))
            elif atom["source_excerpt"] not in section:
                errors.append(_error(f"atoms[{position}] {ident} source_excerpt is not verbatim in its anchored source section"))
    row_map: dict[str, tuple[int, list[str]]] = {}
    for line, cells in rows:
        if len(cells) != 9: continue
        ident = cells[0]
        if ident in row_map:
            errors.append(_error(f"duplicate ledger ID: {ident} (lines {row_map[ident][0]} and {line})"))
        else: row_map[ident] = (line, cells)
    ledger_ids, manifest_ids, index_ids = set(row_map), set(manifest) if isinstance(manifest, dict) else set(), set(index_map)
    if not isinstance(manifest, dict): errors.append(_error("manifest must be an object keyed by ID"))
    for label, expected in (("ledger", ledger_ids), ("manifest", manifest_ids)):
        missing, extra = sorted(expected-index_ids), sorted(index_ids-expected)
        if missing: errors.append(_error(f"index missing {label} ID(s): {', '.join(missing)}"))
        if extra: errors.append(_error(f"index has ID(s) absent from {label}: {', '.join(extra)}"))
    if ledger_ids != manifest_ids:
        missing, extra = sorted(ledger_ids-manifest_ids), sorted(manifest_ids-ledger_ids)
        if missing: errors.append(_error(f"manifest missing ledger ID(s): {', '.join(missing)}"))
        if extra: errors.append(_error(f"manifest has ID(s) absent from ledger: {', '.join(extra)}"))
    # Independent comparisons: source comes from the ledger; anchor from manifest.
    for ident in sorted(index_ids & ledger_ids):
        position, atom = index_map[ident]; line, cells = row_map[ident]
        if atom.get("source") != cells[1]: errors.append(_error(f"atoms[{position}] {ident} source does not match ledger line {line}"))
    for ident in sorted(index_ids & manifest_ids):
        position, atom = index_map[ident]; manifest_atom = manifest.get(ident)
        if not isinstance(manifest_atom, dict):
            errors.append(_error(f"manifest atom {ident} must be an object for anchor comparison")); continue
        if atom.get("source_anchor") != manifest_atom.get("source_anchor"):
            errors.append(_error(f"atoms[{position}] {ident} source_anchor does not match manifest"))
    return errors
