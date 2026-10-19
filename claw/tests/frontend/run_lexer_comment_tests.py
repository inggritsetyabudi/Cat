#!/usr/bin/env python3
"""Block-comment EOF rejection before every public compiler pipeline stage."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile

FIXTURES = Path(__file__).resolve().parent / "fixtures"
MODES = ("check", "validate", "air", "emit-air", "oir", "emit-oir",
         "lir", "emit-lir", "llvm", "emit-llvm", "build", "build-native", "doc")
BUILDS = ("build", "build-native")
ENV = dict(os.environ, NO_COLOR="1", TERM="xterm")
HEADER = "error[lexer]: Unterminated block comment."
BUILD_SOURCE = "error: Unterminated block comment."
WINDOWS_NATIVE = os.name == "nt" or sys.platform.startswith(("msys", "cygwin"))


def diagnostic_path(path: Path) -> str:
    # MSYS Python sees /tmp while the native UCRT64 compiler reports D:\... .
    # Compare the full equivalent native path, not just a basename or suffix.
    if os.name != "nt" and WINDOWS_NATIVE:
        converted = subprocess.run(["cygpath", "-w", str(path)], capture_output=True,
                                   check=True, timeout=10)
        return converted.stdout.decode("utf-8").strip()
    return str(path)


def run(args: list[str], root: Path) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(args, cwd=root, capture_output=True, env=ENV,
                          timeout=30, check=False)


def files(root: Path) -> set[str]:
    return {str(path.relative_to(root)) for path in root.rglob("*")}


def main() -> int:
    compiler = str(Path(sys.argv[1]).resolve())
    cases = [
        ("trailing", (FIXTURES / "lexer_unterminated_trailing.cat").read_bytes(), 4, 1),
        ("only", (FIXTURES / "lexer_unterminated_only.cat").read_bytes(), 1, 1),
        ("nested", (FIXTURES / "lexer_unterminated_nested.cat").read_bytes(), 4, 1),
        ("crlf", (FIXTURES / "lexer_unterminated_trailing.cat").read_bytes()
         .replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"), 4, 1),
        ("utf8", (FIXTURES / "lexer_unterminated_utf8.cat").read_bytes(), 5, 1),
        ("bare_delimiter", b"/*", 1, 1),
        ("trailing_no_newline", b"fn main() {} /*", 1, 14),
        ("body", b"fn main() {\n    /*", 2, 5),
        ("expression", b"fn main() -> Int32 {\n    return 1 + /*", 2, 16),
        ("parameters", b"fn main(/*", 1, 9),
        ("nested_inner_unclosed", b"/* outer\n/* inner", 1, 1),
        ("closed_then_unclosed", b"/* closed */ /*", 1, 14),
        ("utf8_same_line", 'fn main() { println("é") } /*'.encode(), 1, 29),
    ]
    assert b"\r\n" in cases[3][1], "CRLF fixture must retain actual CRLF bytes"
    with tempfile.TemporaryDirectory(prefix="claw-comment-regression-") as temporary:
        root = Path(temporary)
        for name, source, line, column in cases:
            case = root / name
            case.mkdir()
            path = case / "main.cat"
            path.write_bytes(source)
            original = files(case)
            for mode in MODES:
                args = [compiler, mode, str(path)]
                if mode in BUILDS:
                    args.append(str(case / "output.exe"))
                result = run(args, case)
                stderr = result.stderr.decode("utf-8").replace("\r", "")
                location = f"{diagnostic_path(path)}:{line}:{column}"
                # `doc` uses forward slashes; project loading uses native ones.
                # Normalize only path separators, keeping one exact full location.
                locations = [part[5:].replace("\\", "/") for part in stderr.splitlines()
                             if part.startswith(" --> ")]
                headers = [part for part in stderr.splitlines()
                           if part.startswith("error")]
                if mode in BUILDS:
                    expected_headers = [BUILD_SOURCE,
                                        f"error: build failed for `{path.stem}`"]
                else:
                    expected_headers = [HEADER]
                valid = (result.returncode == 1 and not result.stdout
                         and headers == expected_headers
                         and locations == [location.replace("\\", "/")]
                         and any(part.strip() == "|" + " " * column + "^^"
                                 for part in stderr.splitlines())
                         and b"\x1b" not in result.stderr
                         and files(case) == original)
                if not valid:
                    raise AssertionError(f"{name}/{mode}: expected one lexer error at {location}, "
                                         f"empty stdout, exact opening span, no artifacts/ANSI\n"
                                         f"exit={result.returncode}, stdout={result.stdout!r}\n"
                                         f"stderr={stderr}\nfiles={files(case)}")
            print(f"[lexer-comment/pass] {name}: {len(MODES)} modes, span, NO_COLOR, no artifacts")

        # Preserve existing closed/nested behavior; this does not ratify new grammar.
        valid = root / "valid comments"
        valid.mkdir()
        source = valid / "main.cat"
        source.write_bytes((FIXTURES / "lexer_nested_valid.cat").read_bytes())
        for mode in MODES:
            args = [compiler, mode, str(source)]
            exe = valid / f"{mode}.exe"
            if mode in BUILDS:
                args.append(str(exe))
            result = run(args, valid)
            if result.returncode != 0 or (mode not in BUILDS and result.stderr):
                raise AssertionError(f"valid/{mode}: {result.returncode}\n{result.stdout!r}\n{result.stderr!r}")
            if mode in BUILDS:
                assert exe.is_file() and exe.with_suffix(".ll").is_file()
                target = run([str(exe)], valid)
                expected = b"4\n7\n2\n/* not a comment */\n"
                if WINDOWS_NATIVE:
                    expected = expected.replace(b"\n", b"\r\n")
                assert target.returncode == 0 and target.stdout == expected and not target.stderr, target
        # EOF after a complete comment is valid even without a final newline.
        source.write_bytes(b"fn main() {} /* closed */")
        assert run([compiler, "check", str(source)], valid).returncode == 0
        print("[lexer-comment/pass] valid closed/nested/adjacent comments: all modes and exact native output")

        if os.name == "posix":
            # NO_COLOR must work on an actual terminal, not just a redirected pipe.
            command = [compiler, "check", str(root / "trailing" / "main.cat")]
            if WINDOWS_NATIVE:
                sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "cli"))
                from msys_terminal import build_keeper, capture
                keeper = build_keeper(root / "terminal-helper")
                captured = capture(command, ENV, keeper, timeout=30)
                result = subprocess.CompletedProcess(command, captured.returncode)
                output = captured.output
            else:
                import pty
                master, slave = pty.openpty()
                try:
                    result = subprocess.run(command, stdout=slave, stderr=slave,
                                            env=ENV, timeout=30, check=False)
                    os.close(slave)
                    slave = -1
                    output = b""
                    while True:
                        try:
                            part = os.read(master, 65536)
                        except OSError:
                            break
                        if not part:
                            break
                        output += part
                finally:
                    if slave != -1:
                        os.close(slave)
                    os.close(master)
            assert result.returncode == 1 and HEADER.encode() in output and b"\x1b" not in output, output
            print("[lexer-comment/pass] NO_COLOR on PTY")
    print(f"Lexer comment gate passed: {len(cases)} negative cases x {len(MODES)} modes, positive native execution")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
