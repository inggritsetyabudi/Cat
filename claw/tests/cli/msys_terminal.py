"""Real MSYS native-console capture with a separate, retained startup phase.

A native keeper establishes the console before the measured process starts.
Every measured byte is retained, including ESC and CSI-6n; no output is filtered.
"""
from __future__ import annotations

from dataclasses import dataclass
import errno
import os
from pathlib import Path
import pty
import queue
import select
import subprocess
import threading
import time
import tty
from typing import Mapping, Sequence


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


def capture(command: Sequence[str], env: Mapping[str, str], keeper: str,
            timeout: float = 30) -> CaptureResult:
    """Initialize a fresh console, then capture one command without byte filtering.

    Only startup requests receive a cursor reply: the fresh terminal starts at
    row 1/column 1. Measured queries are neither answered nor removed. The helper
    proves native _isatty and GetConsoleMode, and remains alive through capture.
    Startup readiness and stream quiescence are required before measurement;
    any late startup bytes fail the caller's strict assertion, never disappear.
    """
    master, slave = pty.openpty()
    startup, measured = bytearray(), bytearray()
    lock = threading.Lock()
    phase = "startup"
    activity = time.monotonic()
    errors: list[BaseException] = []
    stop = threading.Event()
    cancelled = threading.Event()
    processes: list[subprocess.Popen[bytes]] = []
    launch_lock = threading.Lock()
    launches: list[threading.Thread] = []
    results: queue.Queue[object] = queue.Queue()
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
                try:
                    chunk = os.read(master, 8192)
                except OSError as error:
                    if error.errno == errno.EIO:
                        return
                    raise
                if not chunk:
                    return
                with lock:
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
        # Popen itself can block inside MSYS startup; the pump must run first.
        def run() -> None:
            try:
                process = subprocess.Popen(args, env=dict(env), close_fds=True, **kwargs)
                with launch_lock:
                    processes.append(process)
                    if cancelled.is_set():
                        process.kill()
                if cancelled.is_set():
                    process.wait()
                results.put(process)
            except BaseException as error:
                results.put(error)
        thread = threading.Thread(target=run, daemon=True)
        launches.append(thread)
        thread.start()
        while True:
            try:
                value = results.get(timeout=min(0.02, remaining()))
                if isinstance(value, BaseException):
                    raise value
                return value  # type: ignore[return-value]
            except queue.Empty:
                pass

    def settle(require_marker: bool = False) -> None:
        while True:
            remaining()
            with lock:
                ready = not require_marker or b"MSYS-CONSOLE-KEEPER" in startup
                quiet = time.monotonic() - activity >= 0.10
            if ready and quiet:
                return
            time.sleep(0.01)

    thread = threading.Thread(target=pump, name="msys-terminal-pump", daemon=True)
    try:
        tty.setraw(slave)
        thread.start()
        helper = launch([keeper, "--hold"], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=slave)
        ready_bytes = bytearray()
        while b"READY" not in ready_bytes.splitlines():
            remaining()
            if select.select([helper.stdout], [], [], 0.02)[0]:
                part = os.read(helper.stdout.fileno(), 4096)
                if not part:
                    raise RuntimeError(f"native console keeper exited before READY: {helper.poll()}")
                ready_bytes.extend(part)
        settle(require_marker=True)
        with lock:
            phase = "measured"
        child = launch(list(command), stdin=subprocess.DEVNULL, stdout=slave, stderr=slave)
        os.close(slave)
        slave = -1
        while child.poll() is None:
            remaining()
            time.sleep(0.01)
        # Wait for the concurrently pumped stream to drain, not a fixed sleep
        # that could truncate a large output. The keeper is still alive here.
        settle()
        with lock:
            result = CaptureResult(child.returncode, bytes(measured), bytes(startup))
        return result
    finally:
        cancelled.set()
        for launcher in launches:
            launcher.join(timeout=2)
        with launch_lock:
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
