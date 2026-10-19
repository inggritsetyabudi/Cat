#!/usr/bin/env python3
"""Run one command with stderr attached to a pseudo-terminal and save both streams."""
import errno
import os
import select
import subprocess
import sys
import tempfile
import time


def main() -> int:
    if len(sys.argv) < 6 or sys.argv[4] != "--":
        print(
            "usage: capture_pty.py STDERR_FILE STDOUT_FILE MODE -- COMMAND [ARG ...]",
            file=sys.stderr,
        )
        return 2
    stderr_path, stdout_path, mode = sys.argv[1:4]
    command = sys.argv[5:]
    try:
        import pty
        import tty
    except ImportError:
        print("PTY capture is not supported on this platform; skipping.", file=sys.stderr)
        return 77

    env = os.environ.copy()
    if mode == "no-color":
        env["NO_COLOR"] = "1"
    elif mode != "color":
        print(f"unknown PTY test mode: {mode}", file=sys.stderr)
        return 2

    is_msys = sys.platform.startswith(("msys", "cygwin"))
    if is_msys:
        # Native MSYS launches perform a console startup handshake on the PTY.
        # Keep that protocol outside the command's measured stderr; do not
        # filter bytes from the measured stream itself.
        from pathlib import Path

        from msys_terminal import build_keeper, capture

        try:
            with tempfile.TemporaryDirectory(prefix="claw-capture-pty-") as temporary:
                keeper = build_keeper(Path(temporary) / "native-helper")
                with open(stdout_path, "wb") as stdout_file:
                    result = capture(command, env, keeper, timeout=60,
                                     stdout=stdout_file)
                with open(stderr_path, "wb") as stderr_file:
                    stderr_file.write(result.output)
                with open(stderr_path + ".startup", "wb") as startup_file:
                    startup_file.write(result.startup_output)
                return result.returncode
        except (OSError, RuntimeError, TimeoutError) as error:
            print(f"PTY capture failed: {error}", file=sys.stderr)
            return 1

    try:
        master, slave = pty.openpty()
        tty.setraw(slave)
    except (OSError, AttributeError) as error:
        print(f"PTY capture is unavailable: {error}; skipping.", file=sys.stderr)
        return 77

    output = bytearray()
    try:
        with open(stdout_path, "wb") as stdout_file:
            process = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=stdout_file,
                stderr=slave,
                env=env,
                close_fds=True,
            )
        os.close(slave)
        deadline = time.monotonic() + 60
        while True:
            if time.monotonic() >= deadline:
                process.kill()
                process.wait()
                raise TimeoutError("PTY command exceeded 60 seconds")
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                try:
                    chunk = os.read(master, 8192)
                    if chunk:
                        output.extend(chunk)
                    elif process.poll() is not None:
                        break
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
            if process.poll() is not None and not ready:
                try:
                    chunk = os.read(master, 8192)
                    if chunk:
                        output.extend(chunk)
                    else:
                        break
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
        return_code = process.wait()
    finally:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.close(slave)
        except OSError:
            pass

    with open(stderr_path, "wb") as stderr_file:
        stderr_file.write(output)
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
