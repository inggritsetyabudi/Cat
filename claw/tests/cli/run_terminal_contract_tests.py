#!/usr/bin/env python3
"""Raw terminal capture contract: startup is separate; measured ESC is never removed."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
MSYS = sys.platform.startswith(("msys", "cygwin"))
ENV = dict(os.environ, NO_COLOR="1", TERM="xterm")


def main() -> int:
    from msys_terminal import validate_startup
    known_startup = (b"\x1b[6n\x1b[6n\x1b[?1004h\x1b[m\x1b[?25h"
                     b"MSYS-CONSOLE-KEEPER\r\n")
    validate_startup(known_startup)
    for unexpected in (known_startup + b"\x1b[6n", known_startup + b"UNEXPECTED-STARTUP\n"):
        try:
            validate_startup(unexpected)
        except RuntimeError:
            pass
        else:
            raise AssertionError("unexpected startup bytes must fail closed")
    print("[terminal-contract/pass] complete startup protocol is validated, not silently discarded")
    with tempfile.TemporaryDirectory(prefix="claw-terminal-contract-") as temporary:
        root = Path(temporary)
        keeper = None
        if MSYS:
            from msys_terminal import build_keeper, capture
            keeper = build_keeper(root / "native-helper")

        def terminal(command: list[str]) -> tuple[int, bytes]:
            if MSYS:
                captured = capture(command, ENV, keeper, timeout=10)
                validate_startup(captured.startup_output)
                return captured.returncode, captured.output
            stderr, stdout = root / "stderr", root / "stdout"
            result = subprocess.run([sys.executable, str(HERE / "capture_pty.py"),
                                     str(stderr), str(stdout), "no-color", "--", *command],
                                    env=ENV, capture_output=True, timeout=20, check=False)
            assert stdout.read_bytes() == b"", stdout.read_bytes()
            return result.returncode, stderr.read_bytes()

        if MSYS:
            rc, output = terminal([keeper, "--probe"])
            assert rc == 0 and output.replace(b"\r\n", b"\n") == b"TERMINAL-PROBE tty=1 console=1\n", (rc, output)
            print("[terminal-contract/pass] native stderr is a real Windows console")
            rc, output = terminal([keeper, "--silent"])
        else:
            rc, output = terminal([sys.executable, "-c", "import os; assert os.isatty(2)"])
        assert rc == 0 and output == b"", (rc, output)
        print("[terminal-contract/pass] terminal startup cannot contaminate a silent measured child")

        for payload in (b"TERMINAL-PLAIN\n", b"\x1b[31mTERMINAL-ANSI\x1b[0m\n", b"\x1b[6n"):
            command = [sys.executable, "-c", f"import os; os.write(2, {payload!r})"]
            rc, output = terminal(command)
            assert rc == 0 and output == payload, (payload, rc, output)
            if b"\x1b" in payload:
                assert not (b"\x1b" not in output), "the protected no-ESC assertion must reject this output"
            print(f"[terminal-contract/pass] measured bytes preserved exactly: {payload!r}")

        rc, output = terminal([sys.executable, "-c", "raise SystemExit(23)"])
        assert rc == 23 and output == b"", (rc, output)
        print("[terminal-contract/pass] child exit status is preserved")

        payload = b"x" * 131072
        rc, output = terminal([sys.executable, "-c", "import os; os.write(2, b'x' * 131072)"])
        assert rc == 0 and output == payload, (rc, len(output))
        print("[terminal-contract/pass] large output is drained without truncation")
        if MSYS:
            try:
                capture([keeper, "--sleep"], ENV, keeper, timeout=0.5)
            except TimeoutError:
                pass
            else:
                raise AssertionError("a stalled native child must time out")
            rc, output = terminal([keeper, "--silent"])
            assert rc == 0 and output == b"", (rc, output)
            print("[terminal-contract/pass] timeout cleanup allows a clean subsequent capture")
            for phase in ("before", "after"):
                delayed_env = dict(ENV, CLAW_TERMINAL_TEST_LATE_STARTUP=phase)
                try:
                    delayed = capture([keeper, "--silent"], delayed_env, keeper, timeout=10)
                except RuntimeError as error:
                    assert "unexpected native terminal startup bytes" in str(error), error
                else:
                    assert b"UNEXPECTED-STARTUP" in delayed.output, delayed
                    assert delayed.output != b"", "late startup must fail the silent-child predicate"
            print("[terminal-contract/pass] bytes injected before/after the startup barrier cannot disappear")

        compiler = os.environ.get("CLAW_EXE")
        if not compiler:
            raise RuntimeError("CLAW_EXE is required for the actual compiler terminal regression")
        source = root / "main.cat"
        source.write_bytes((HERE.parent / "frontend" / "fixtures" /
                            "lexer_unterminated_trailing.cat").read_bytes())
        rc, output = terminal([str(Path(compiler).resolve()), "check", str(source)])
        assert rc == 1 and b"error[lexer]: Unterminated block comment." in output and b"\x1b" not in output, output
        print("[terminal-contract/pass] exact compiler fixture and unchanged NO_COLOR/no-ESC predicate")
    print("Terminal transport contract gate passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
