#!/usr/bin/env python3
"""Run executable C@ examples embedded in the approved PRD.

Supported fence labels:
  ```cat                         the block must pass `claw check`
  ```cat error: <substring>     the block must fail and report the substring
  ```cat proposal               syntax not implemented by the compiler; skipped
  ```text                        non-C@ material; skipped

Every fence must have one of these labels. Skipped blocks and their opening
lines are reported explicitly; they are not counted as compiler validation.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


FENCE_OPEN = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
FENCE_CLOSE = re.compile(r"^ {0,3}(`+|~+)[ \t]*$")


def read_fences(path: Path):
    """Yield (opening_line, info_string, body) for Markdown fenced blocks."""
    lines = path.read_text(encoding="utf-8").replace("\r\n", "\n").splitlines()
    index = 0
    while index < len(lines):
        match = FENCE_OPEN.match(lines[index])
        if not match:
            index += 1
            continue

        opening_line = index + 1
        fence = match.group(1)
        info = match.group(2).strip()
        index += 1
        body = []
        while index < len(lines):
            closing = FENCE_CLOSE.match(lines[index])
            if (
                closing
                and closing.group(1)[0] == fence[0]
                and len(closing.group(1)) >= len(fence)
            ):
                break
            body.append(lines[index])
            index += 1
        if index == len(lines):
            raise ValueError(f"unclosed fenced block opened at {path}:{opening_line}")
        yield opening_line, info, "\n".join(body) + "\n"
        index += 1


def find_compiler(repo_root: Path) -> str:
    configured = os.environ.get("CLAW_EXE")
    if configured:
        if Path(configured).is_file() and os.access(configured, os.X_OK):
            return str(Path(configured).resolve())
        raise FileNotFoundError(f"CLAW_EXE is not an executable file: {configured}")

    candidates = (
        repo_root / "claw" / "build" / "claw",
        repo_root / "claw" / "build-ucrt64-clang" / "claw.exe",
        repo_root / "claw" / "build-ucrt64-clang" / "claw-codex.exe",
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate.resolve())
    raise FileNotFoundError(
        "compiler executable not found; build claw or set CLAW_EXE to its path"
    )


def format_lines(lines: list[int]) -> str:
    return ", ".join(str(line) for line in lines) if lines else "none"


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <PRD.md>", file=sys.stderr)
        return 2

    prd = Path(sys.argv[1]).resolve()
    repo_root = prd.parent.parent
    try:
        compiler = find_compiler(repo_root)
        fences = list(read_fences(prd))
    except (OSError, ValueError) as error:
        print(f"doc-test setup failed: {error}", file=sys.stderr)
        return 2

    tested = 0
    passed = 0
    rejected = 0
    proposal_lines: list[int] = []
    text_lines: list[int] = []
    untagged_lines: list[int] = []
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="cat-prd-doc-test-") as temporary:
        temp_dir = Path(temporary)
        for block_number, (line, info, body) in enumerate(fences, start=1):
            if not info:
                untagged_lines.append(line)
                failures.append(
                    f"block #{block_number} at line {line}: missing fence label; "
                    "use `cat`, `cat error: ...`, `cat proposal`, or `text`"
                )
                continue
            if info == "text":
                text_lines.append(line)
                continue
            if info == "cat proposal":
                proposal_lines.append(line)
                continue
            if info == "cat":
                expected_error = None
            elif info.startswith("cat error:"):
                expected_error = info[len("cat error:"):].strip()
                if not expected_error:
                    failures.append(
                        f"block #{block_number} at line {line}: empty expected diagnostic"
                    )
                    continue
            else:
                failures.append(
                    f"block #{block_number} at line {line}: unsupported fence label {info!r}; "
                    "use `cat`, `cat error: ...`, `cat proposal`, or `text`"
                )
                continue

            tested += 1
            source = temp_dir / f"block-{block_number}.cat"
            source.write_text(body, encoding="utf-8")
            try:
                result = subprocess.run(
                    [compiler, "check", str(source)],
                    capture_output=True,
                    text=True,
                    timeout=30,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired) as error:
                failures.append(
                    f"block #{block_number} at line {line}: compiler invocation failed: {error}"
                )
                continue

            output = result.stdout + result.stderr
            if expected_error is None:
                if result.returncode == 0:
                    passed += 1
                else:
                    failures.append(
                        f"block #{block_number} at line {line}: expected success, "
                        f"got exit {result.returncode}\n{output}"
                    )
            elif result.returncode != 0 and expected_error in output:
                rejected += 1
            else:
                failures.append(
                    f"block #{block_number} at line {line}: expected rejection containing "
                    f"{expected_error!r}, got exit {result.returncode}\n{output}"
                )

    if tested == 0 and not failures:
        failures.append("no executable `cat` or `cat error:` blocks found")

    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)

    status = "COMPLETE" if not failures and not untagged_lines else "INCOMPLETE"
    print(
        f"PRD doc-test STATUS: {status} — {len(fences)} fences classified; "
        f"{tested} executable blocks checked ({passed} passed, {rejected} expected rejection"
        f"{'s' if rejected != 1 else ''}); "
        f"{len(proposal_lines)} proposal blocks skipped (lines {format_lines(proposal_lines)}); "
        f"{len(text_lines)} text blocks skipped (lines {format_lines(text_lines)}); "
        f"{len(untagged_lines)} untagged (lines {format_lines(untagged_lines)})."
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
