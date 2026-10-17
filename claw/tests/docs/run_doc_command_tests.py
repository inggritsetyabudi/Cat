#!/usr/bin/env python3
"""Golden CLI tests for top-level documentation comments and `claw doc`."""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def find_compiler(repo_root: Path) -> Path:
    configured = os.environ.get("CLAW_EXE")
    if configured:
        path = Path(configured)
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
        raise SystemExit(f"CLAW_EXE is not an executable file: {configured}")

    candidates = (
        repo_root / "claw" / "build" / "claw",
        repo_root / "claw" / "build-ucrt64-clang" / "claw.exe",
        repo_root / "claw" / "build-ucrt64-clang" / "claw-codex.exe",
    )
    for path in candidates:
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    raise SystemExit("compiler executable not found; build claw or set CLAW_EXE")


def run(compiler: Path, *args: str) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            [str(compiler), *args],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"compiler invocation failed: {error}") from error


def require(condition: bool, message: str, result: subprocess.CompletedProcess[str] | None = None) -> None:
    if condition:
        return
    if result is not None:
        message += f"\nexit={result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    raise SystemExit(f"FAIL: {message}")


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    compiler = find_compiler(repo_root)

    with tempfile.TemporaryDirectory(prefix="claw-doc-cli-") as temporary:
        temp = Path(temporary)

        render_source = temp / "render.cat"
        render_source.write_text(
            "/// First line\n"
            "\n"
            "// an ordinary line comment does not break attachment\n"
            "/* nor does a block comment */\n"
            "///  second line keeps one leading space\n"
            "share fn alpha() -> Int32 {\n"
            "    /// nested documentation remains an ordinary comment\n"
            "    return 1\n"
            "}\n"
            "\n"
            "/// Shape docs\n"
            "share shape PublicShape {\n"
            "  value: Int32\n"
            "}\n"
            "///Choice docs\n"
            "share choice PublicChoice {\n"
            "  ready\n"
            "  busy\n"
            "}\n"
            "share fn undocumented() { }\n",
            encoding="utf-8",
        )
        expected = (
            "# alpha\n\n"
            "First line\n"
            " second line keeps one leading space\n\n"
            "# PublicShape\n\n"
            "Shape docs\n\n"
            "# PublicChoice\n\n"
            "Choice docs\n"
        )
        result = run(compiler, "doc", str(render_source))
        require(result.returncode == 0, "documented shared declarations should render successfully", result)
        require(result.stdout == expected, "Markdown output differs from the golden", result)
        require(result.stderr == "", "clean documented source unexpectedly emitted diagnostics", result)
        repeated = run(compiler, "doc", str(render_source))
        require(repeated.returncode == 0 and repeated.stdout == expected and repeated.stderr == "",
                "repeated generation must be deterministic", repeated)
        check = run(compiler, "check", str(render_source))
        require(check.returncode == 0, "ordinary and nested comments must remain accepted by `claw check`", check)
        print("[doc-cli/pass] golden output, declaration order, repeated docs, spacing, and nested comments")

        nonshare_source = temp / "nonshare.cat"
        nonshare_source.write_text(
            "/// private summary\n"
            "fn private_fn() { }\n"
            "share fn public_without_docs() { }\n",
            encoding="utf-8",
        )
        result = run(compiler, "doc", str(nonshare_source))
        require(result.returncode == 0, "a non-share documentation warning must be non-fatal", result)
        require(result.stdout == "", "an undocumented shared item or non-share item must not render", result)
        require("warning[doc]" in result.stderr and "non-share declaration 'private_fn'" in result.stderr,
                "expected warning for documentation attached to a non-share declaration", result)
        require(f"{nonshare_source}:1:1" in result.stderr, "warning should point to the doc comment location", result)
        require("# public_without_docs" not in result.stdout,
                "documentation must not falsely attach across an intervening declaration", result)
        check = run(compiler, "check", str(nonshare_source))
        require(check.returncode == 0 and "warning[doc]" in check.stderr,
                "parser documentation warnings should also be surfaced by `claw check`", check)
        print("[doc-cli/pass] non-share warning is non-fatal and docs do not jump declarations")

        orphan_source = temp / "orphan.cat"
        orphan_source.write_text("/* block comment */\n/// orphan docs\n// trailing comment\n", encoding="utf-8")
        result = run(compiler, "doc", str(orphan_source))
        require(result.returncode == 0, "orphan documentation must warn rather than fail", result)
        require(result.stdout == "", "orphan documentation must not produce Markdown", result)
        require("warning[doc]" in result.stderr and "no following top-level declaration" in result.stderr,
                "expected orphan documentation warning", result)
        require(f"{orphan_source}:2:1" in result.stderr, "orphan warning should point to its first doc line", result)
        print("[doc-cli/pass] orphan docs warn non-fatally with source location")

        malformed_source = temp / "malformed.cat"
        malformed_source.write_text("/// malformed declaration\nshare fn broken( { }\n", encoding="utf-8")
        result = run(compiler, "doc", str(malformed_source))
        require(result.returncode == 1, "parse errors in `claw doc` must fail", result)
        require(result.stdout == "" and "error[parse]" in result.stderr,
                "parse errors should be reported without Markdown output", result)
        print("[doc-cli/pass] malformed source exits nonzero")

        comments_source = temp / "comments-location.cat"
        comments_source.write_text("/* block header */\n// ordinary header\nfn broken( {\n", encoding="utf-8")
        result = run(compiler, "doc", str(comments_source))
        require(result.returncode == 1, "ordinary comments must not hide later syntax errors", result)
        require(f"{comments_source}:3:" in result.stderr,
                "ordinary comments must preserve later source line locations", result)
        print("[doc-cli/pass] ordinary comments retain parser source locations")

        result = run(compiler, "doc", str(render_source), str(temp / "ignored.md"))
        require(result.returncode == 1 and "Usage: claw doc <source.cat>" in result.stderr,
                "`claw doc` must reject an extra output argument", result)
        print("[doc-cli/pass] command argument contract")

    print("Documentation command CLI tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
