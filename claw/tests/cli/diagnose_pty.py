#!/usr/bin/env python3
"""Temporary unfiltered PTY diagnostics; not a replacement for any gate."""
import errno
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import tty


def capture(command, stdin_mode="inherit", stdout_mode="pty", raw=False, extra_env=None):
    master, slave = pty.openpty()
    if raw:
        tty.setraw(slave)
    env = dict(os.environ, NO_COLOR="1", TERM="xterm")
    env.update(extra_env or {})
    try:
        with tempfile.TemporaryFile() as stdout:
            result = subprocess.run(command, stdin={"inherit": None, "slave": slave, "null": subprocess.DEVNULL}[stdin_mode],
                                    stdout=slave if stdout_mode == "pty" else stdout,
                                    stderr=slave, env=env, timeout=30, check=False)
            os.close(slave)
            slave = -1
            output = b""
            while True:
                try:
                    part = os.read(master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not part:
                    break
                output += part
            stdout.seek(0)
            return result.returncode, output, stdout.read()
    finally:
        if slave != -1:
            os.close(slave)
        os.close(master)


def main():
    compiler, silent, marker = map(str, map(Path, sys.argv[1:]))
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / "main.cat"
        source.write_bytes((Path(__file__).resolve().parents[1] / "frontend" / "fixtures" /
                            "lexer_unterminated_trailing.cat").read_bytes())
        source_arg = subprocess.check_output(["cygpath", "-w", str(source)], text=True).strip() if sys.platform.startswith(("msys", "cygwin")) else str(source)
        for command in ([silent], [marker], [compiler, "check", source_arg]):
            result = subprocess.run(command, capture_output=True, env=dict(os.environ, NO_COLOR="1", TERM="xterm"), timeout=30, check=False)
            print("PIPE CONTROL", command, result.returncode, repr(result.stdout), repr(result.stderr), flush=True)
        for command in ([silent], [marker], [compiler, "check", source_arg]):
            for stdin_mode, stdout_mode, raw in [("inherit", "pty", False), ("null", "pty", False), ("slave", "pty", False), ("inherit", "file", False), ("null", "file", True), ("slave", "pty", True)]:
                print("PROBE", command, stdin_mode, stdout_mode, "raw", raw, capture(command, stdin_mode, stdout_mode, raw), flush=True)
        rc, output, stdout = capture([compiler, "check", source_arg])
        print("EXACT ORIGINAL PTY ASSERTION", rc == 1 and b"error[lexer]: Unterminated block comment." in output and b"\x1b" not in output, repr(output), flush=True)


if __name__ == "__main__":
    main()
