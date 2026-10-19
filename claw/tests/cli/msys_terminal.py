"""Real MSYS native-console capture; startup and measured bytes are retained."""
from __future__ import annotations

from dataclasses import dataclass
import errno
import os
from pathlib import Path
import pty
import re
import select
import subprocess
import threading
import time
import tty
from typing import BinaryIO, Mapping, Sequence


@dataclass(frozen=True)
class CaptureResult:
    returncode: int
    output: bytes
    startup_output: bytes


def _cygpath(path: Path) -> str:
    return subprocess.run(["cygpath", "-w", str(path)], check=True,
                          capture_output=True, text=True, timeout=30).stdout.strip()


def build_keeper(directory: Path) -> str:
    """Build the small native fixture, never a Cygwin/MSYS executable."""
    directory.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name("fixtures") / "msys_console_keeper.c"
    output = directory / "msys_console_keeper.exe"
    subprocess.run(["clang", "-O2", _cygpath(source), "-o", _cygpath(output)],
                   check=True, timeout=30)
    return str(output)


def validate_startup(output: bytes) -> None:
    """Fail closed on unexpected startup bytes; never validate measured output here.

    This is the observed MSYS native-console handshake, followed by our keeper
    marker. Changes in runtime protocol require investigation, not auto-filtering.
    """
    pattern = (rb"(?:\x1b\[6n){1,2}\x1b\[\?1004h\x1b\[m\x1b\[\?25h"
               rb"MSYS-CONSOLE-KEEPER\r?\n")
    if re.fullmatch(pattern, output) is None:
        raise RuntimeError(f"unexpected native terminal startup bytes: {output!r}")


def capture(command: Sequence[str], env: Mapping[str, str], keeper: str,
            timeout: float = 30, stdout: BinaryIO | int | None = None) -> CaptureResult:
    """Establish a real console and acknowledge its barrier before launching command.

    All startup bytes must match the complete known handshake; all later bytes
    are returned untouched. Only startup CSI-6n receives a reply (a fresh terminal
    starts at row 1/column 1). The measured process is not launched until after
    the buffer transition. A late startup byte therefore either fails startup
    validation or appears in measured output, never being silently removed.

    The pump starts before synchronous Popen to service MSYS native startup.
    Process creation itself, as with Python subprocess.run, is an OS operation;
    there are no background launchers that can outlive cleanup.
    """
    master, slave = pty.openpty()
    startup, measured = bytearray(), bytearray()
    lock = threading.Lock()
    phase = "startup"
    activity = time.monotonic()
    errors: list[BaseException] = []
    stop = threading.Event()
    processes: list[subprocess.Popen[bytes]] = []
    deadline = time.monotonic() + timeout

    def remaining() -> float:
        value = deadline - time.monotonic()
        if value <= 0:
            raise TimeoutError("MSYS terminal capture exceeded its deadline")
        if errors:
            raise RuntimeError("MSYS terminal pump failed") from errors[0]
        return value

    def pump() -> None:
        nonlocal activity
        pending = b""
        try:
            while not stop.is_set():
                if not select.select([master], [], [], 0.02)[0]:
                    continue
                with lock:
                    try:
                        chunk = os.read(master, 8192)
                    except OSError as error:
                        if error.errno == errno.EIO:
                            return
                        raise
                    if not chunk:
                        return
                    activity = time.monotonic()
                    if phase == "startup":
                        startup.extend(chunk)
                        pending += chunk
                        count = pending.count(b"\x1b[6n")
                        pending = pending[pending.rfind(b"\x1b[6n") + 4:] if count else pending[-3:]
                    else:
                        measured.extend(chunk)
                        count = 0
                for _ in range(count):
                    os.write(master, b"\x1b[1;1R")
        except BaseException as error:
            errors.append(error)

    def launch(args: Sequence[str], **kwargs: object) -> subprocess.Popen[bytes]:
        process = subprocess.Popen(args, env=dict(env), close_fds=True, **kwargs)
        processes.append(process)
        remaining()
        return process

    def receive_line(process: subprocess.Popen[bytes], expected: bytes) -> None:
        data = bytearray()
        while expected not in data.splitlines():
            remaining()
            if select.select([process.stdout], [], [], 0.02)[0]:
                part = os.read(process.stdout.fileno(), 4096)
                if not part:
                    raise RuntimeError(f"native console keeper exited before {expected!r}: {process.poll()}")
                data.extend(part)

    thread = threading.Thread(target=pump, name="msys-terminal-pump", daemon=True)
    try:
        tty.setraw(slave)
        thread.start()
        helper = launch([keeper, "--hold"], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=slave)
        receive_line(helper, b"READY")
        # The terminal marker is an ordered endpoint of the startup stream.
        while True:
            remaining()
            with lock:
                if startup.endswith((b"MSYS-CONSOLE-KEEPER\n", b"MSYS-CONSOLE-KEEPER\r\n")):
                    break
            time.sleep(0.01)
        helper.stdin.write(b"B")
        helper.stdin.flush()
        receive_line(helper, b"BARRIER")
        with lock:
            validate_startup(bytes(startup))
            phase = "measured"
            activity = time.monotonic()
        child = launch(list(command), stdin=subprocess.DEVNULL,
                       stdout=slave if stdout is None else stdout, stderr=slave)
        os.close(slave)
        slave = -1
        while child.poll() is None:
            remaining()
            time.sleep(0.01)
        # The keeper remains alive while pending command output drains. No
        # teardown bytes or helper marker are used to trim measured output.
        while True:
            remaining()
            with lock:
                quiet = time.monotonic() - activity >= 0.10
                pending_bytes = bool(select.select([master], [], [], 0)[0])
                if quiet and not pending_bytes:
                    return CaptureResult(child.returncode, bytes(measured), bytes(startup))
            time.sleep(0.01)
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
        for process in processes:
            process.wait(timeout=5)
            if process.stdin is not None:
                process.stdin.close()
            if process.stdout is not None:
                process.stdout.close()
        stop.set()
        if thread.ident is not None:
            thread.join(timeout=2)
        if slave != -1:
            os.close(slave)
        os.close(master)
        if thread.is_alive():
            raise RuntimeError("MSYS terminal pump did not stop")
        if errors:
            raise RuntimeError("MSYS terminal pump failed") from errors[0]
