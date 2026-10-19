#!/usr/bin/env python3
"""Diagnostic experiment only: retain every raw byte, including protocol."""
import errno
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import threading
import time
import tty

master, slave = pty.openpty()
tty.setraw(slave)
setup = bytearray()
measured = bytearray()
phase = setup
errors = []
stop = threading.Event()

def pump():
    seen = b""
    answered = 0
    try:
        while not stop.is_set():
            chunk = os.read(master, 8192)
            if not chunk:
                return
            phase.extend(chunk)
            seen += chunk
            count = seen.count(b"\x1b[6n")
            while answered < count:
                # Fresh empty terminal is initialized at row 1, column 1.
                os.write(master, b"\x1b[1;1R")
                answered += 1
    except OSError as error:
        if error.errno != errno.EIO:
            errors.append(repr(error))

thread = threading.Thread(target=pump, daemon=True)
thread.start()
env = dict(os.environ, NO_COLOR="1", TERM="xterm")
helper = None
try:
    helper = subprocess.Popen([sys.argv[2]], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=slave, env=env)
    ready = helper.stdout.readline()
    print("PREFLIGHT READY", repr(ready), flush=True)
    time.sleep(0.3)
    print("PREFLIGHT ALL RAW BYTES", repr(bytes(setup)), flush=True)
    phase = measured
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / "main.cat"
        source.write_bytes(b"/*")
        arg = subprocess.check_output(["cygpath", "-w", str(source)], text=True).strip()
        result = subprocess.run([sys.argv[1], "check", arg], stdin=subprocess.DEVNULL,
                                stdout=slave, stderr=slave, env=env, timeout=30, check=False)
        time.sleep(0.3)
        print("MEASURED ALL RAW BYTES", result.returncode, repr(bytes(measured)), flush=True)
        print("MEASURED UNCHANGED NO ESC", result.returncode == 1 and b"error[lexer]: Unterminated block comment." in measured and b"\x1b" not in measured, flush=True)
finally:
    if helper is not None:
        helper.stdin.write(b"x\n")
        helper.stdin.flush()
        helper.wait(timeout=10)
    stop.set()
    os.close(slave)
    os.close(master)
    thread.join(timeout=2)
    print("PUMP ERRORS", errors, flush=True)
