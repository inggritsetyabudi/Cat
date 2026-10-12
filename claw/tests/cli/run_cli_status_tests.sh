#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
FIXTURE_DIR="$CLAW_DIR/tests/frontend/fixtures"

if [[ -z "${CLAW_EXE:-}" ]]; then
  if [[ -x "$CLAW_DIR/build/claw" ]]; then
    CLAW_EXE="$CLAW_DIR/build/claw"
  elif [[ -x "$CLAW_DIR/build-ucrt64-clang/claw.exe" ]]; then
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw.exe"
  else
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw-codex.exe"
  fi
fi

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  exit 1
fi

CLAW_EXE_DIR="$(cd -- "$(dirname -- "$CLAW_EXE")" && pwd)"
CLAW_EXE="$CLAW_EXE_DIR/$(basename -- "$CLAW_EXE")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/claw-cli-status.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) IS_POSIX=0 ;;
  *) IS_POSIX=1 ;;
esac
fail() {
  echo "CLI status test failed: $*" >&2
  exit 1
}
assert_no_clang_diagnostics() {
  local directory="$1"
  if find "$directory" -maxdepth 1 -type f -name 'claw-clang-*' -print -quit | grep -q .; then
    fail "temporary clang diagnostics were not removed from $directory"
  fi
}

success_output="$TEST_DIR/success.exe"
success_stdout="$TEST_DIR/success.stdout"
success_stderr="$TEST_DIR/success.stderr"
success_tmp="$TEST_DIR/success-tmp"
mkdir -p "$success_tmp"
printf '%s\n' '[cli-status/pass] optimized native build succeeds with truthful phase status'
if ! env TMPDIR="$success_tmp" "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$success_output" >"$success_stdout" 2>"$success_stderr"; then
  cat "$success_stderr" >&2
  fail "valid Cat program did not build"
fi
[[ "$(cat "$success_stdout")" == "Built native executable: $success_output" ]] || fail "success stdout changed"
grep -Fq 'C@ Forge | Native optimized (-O2)' "$success_stderr" || fail "missing Cat Forge profile label"
grep -Fq 'Key: = complete, > active, . waiting' "$success_stderr" || fail "phase-state legend missing"
grep -Fq '[>.....] | Resolve input path' "$success_stderr" || fail "initial phase status missing"
grep -Fq '[=====>] | Lower LLVM + link native code (working; progress is indeterminate)' "$success_stderr" || fail "indeterminate native-link phase missing"
grep -Fq '[======] | FORGE READY | native artifact ready' "$success_stderr" || fail "success completion status missing"
grep -Eq '[0-9]+%' "$success_stderr" && fail "estimated percentage must not be shown"
if grep -q $'\033' "$success_stderr"; then
  fail "ANSI color codes should be omitted when stderr is redirected"
fi
if [[ "$IS_POSIX" -eq 1 ]]; then
  assert_no_clang_diagnostics "$success_tmp"
fi
actual_program_output="$("$success_output" | tr -d '\r')"
[[ "$actual_program_output" == $'42\n0\nC@\nworld' ]] || fail "built Cat program output changed: $actual_program_output"

failure_stdout="$TEST_DIR/failure.stdout"
failure_stderr="$TEST_DIR/failure.stderr"
printf '%s\n' '[cli-status/pass] source error keeps diagnostics and reports failed phase'
set +e
"$CLAW_EXE" build "$FIXTURE_DIR/revise_bad_char_empty.cat" "$TEST_DIR/failure.exe" >"$failure_stdout" 2>"$failure_stderr"
failure_rc=$?
set -e
[[ "$failure_rc" -eq 1 ]] || fail "source error exit code changed: $failure_rc"
[[ ! -s "$failure_stdout" ]] || fail "failed build unexpectedly wrote success stdout"
grep -Fq 'error[parse]: Character literal must contain exactly one Unicode scalar value.' "$failure_stderr" || fail "original parse diagnostic missing"
grep -Fq '[=!....] | FORGE HALTED | compile rejected; see diagnostics' "$failure_stderr" || fail "compile failure status missing"
grep -Fq 'Native optimized (-O2)' "$failure_stderr" || fail "failure omitted active native profile"
if grep -q $'\033' "$failure_stderr"; then
  fail "ANSI color codes should be omitted when stderr is redirected"
fi

missing_stdout="$TEST_DIR/missing.stdout"
missing_stderr="$TEST_DIR/missing.stderr"
printf '%s\n' '[cli-status/pass] unresolved input gets failure status'
set +e
"$CLAW_EXE" build "$TEST_DIR/does-not-exist.cat" "$TEST_DIR/missing.exe" >"$missing_stdout" 2>"$missing_stderr"
missing_rc=$?
set -e
[[ "$missing_rc" -eq 1 ]] || fail "missing input exit code changed: $missing_rc"
grep -Fq '[!.....] | FORGE HALTED | input path unresolved; see details' "$missing_stderr" || fail "early failure status missing"

# A slow fake linker gives the PTY test enough time to observe multiple spinner frames.
FAKE_CLANG_DIR="$TEST_DIR/fake-bin"
mkdir -p "$FAKE_CLANG_DIR"
cat >"$FAKE_CLANG_DIR/clang" <<'SH'
#!/bin/sh
sleep 0.36
  case "${CLAW_TEST_CLANG_MODE:-success}" in
  fail)
    printf '%s\n' 'simulated clang failure: exit 23' >&2
    exit 23
    ;;
  signal)
    ulimit -c 0
    kill -ILL "$$"
    ;;
  large-fail)
    printf '%s\n' 'LARGE-BEGIN' >&2
    python3 -c 'import sys; sys.stderr.write("x" * (4 * 1024 * 1024))'
    printf '\nLARGE-END\n' >&2
    exit 23
    ;;
  replace-diagnostics-path)
    printf '%s\n' 'ORIGINAL-DESCRIPTOR-DIAGNOSTIC' >&2
    set -- "$TMPDIR"/claw-clang-*
    if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
      printf '%s\n' 'could not identify the clang diagnostics tempfile' >&2
      exit 91
    fi
    rm -- "$1"
    printf '%s\n' 'PATHNAME-REPLACEMENT-TRAP' >"$1"
    exit 23
    ;;
  success)
    for argument do output="$argument"; done
    : >"$output"
    ;;
  *)
    printf 'unknown fake clang mode: %s\n' "$CLAW_TEST_CLANG_MODE" >&2
    exit 2
    ;;
esac
SH
chmod +x "$FAKE_CLANG_DIR/clang"

if [[ "$IS_POSIX" -eq 1 ]]; then
  fake_failure_tmp="$TEST_DIR/fake-failure-tmp"
  mkdir -p "$fake_failure_tmp"
  fake_failure_stdout="$TEST_DIR/fake-failure.stdout"
  fake_failure_stderr="$TEST_DIR/fake-failure.stderr"
  printf '%s\n' '[cli-status/pass] exit 23 is classified as a clang process failure with diagnostics preserved'
  set +e
  env TMPDIR="$fake_failure_tmp" PATH="$FAKE_CLANG_DIR:$PATH" CLAW_TEST_CLANG_MODE=fail \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/fake-failure.exe" >"$fake_failure_stdout" 2>"$fake_failure_stderr"
  fake_failure_rc=$?
  set -e
  [[ "$fake_failure_rc" -eq 1 ]] || fail "failed linker changed CLI failure exit code: $fake_failure_rc"
  [[ ! -s "$fake_failure_stdout" ]] || fail "failed linker unexpectedly wrote success stdout"
  grep -Fq 'simulated clang failure: exit 23' "$fake_failure_stderr" || fail "original fake clang diagnostic missing"
  grep -Fq '`clang` exited with status 23' "$fake_failure_stderr" || fail "exit 23 was not classified as a clang process failure"
  if grep -Fq 'Failed to launch' "$fake_failure_stderr"; then
    fail "a running clang process was incorrectly classified as a launch failure"
  fi
  assert_no_clang_diagnostics "$fake_failure_tmp"

  missing_clang_tmp="$TEST_DIR/missing-clang-tmp"
  mkdir -p "$missing_clang_tmp"
  missing_clang_stderr="$TEST_DIR/missing-clang.stderr"
  printf '%s\n' '[cli-status/pass] missing clang is classified as an executable lookup failure, not exit 127'
  set +e
  env TMPDIR="$missing_clang_tmp" PATH="$TEST_DIR/missing-clang-bin" \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/missing-clang.exe" >"$TEST_DIR/missing-clang.stdout" 2>"$missing_clang_stderr"
  missing_clang_rc=$?
  set -e
  [[ "$missing_clang_rc" -eq 1 ]] || fail "missing clang changed CLI failure exit code: $missing_clang_rc"
  grep -Fq '`clang` could not be found or executed from PATH (posix_spawnp: No such file or directory).' "$missing_clang_stderr" || fail "missing clang was not classified as an executable lookup failure"
  if grep -Fq '`clang` exited with status 127' "$missing_clang_stderr"; then
    fail "missing clang was incorrectly classified as a normal exit 127"
  fi
  assert_no_clang_diagnostics "$missing_clang_tmp"

  closed_stderr_tmp="$TEST_DIR/closed-stderr-tmp"
  mkdir -p "$closed_stderr_tmp"
  printf '%s\n' '[cli-status/pass] closed parent stderr does not capture progress into the diagnostics tempfile'
  set +e
  env TMPDIR="$closed_stderr_tmp" PATH="$FAKE_CLANG_DIR:$PATH" CLAW_TEST_CLANG_MODE=fail \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/closed-stderr.exe" >/dev/null 2>&-
  closed_stderr_rc=$?
  set -e
  [[ "$closed_stderr_rc" -eq 1 ]] || fail "closed stderr changed linker failure exit code: $closed_stderr_rc"
  assert_no_clang_diagnostics "$closed_stderr_tmp"

  signal_tmp="$TEST_DIR/signal-tmp"
  mkdir -p "$signal_tmp"
  signal_stderr="$TEST_DIR/signal.stderr"
  printf '%s\n' '[cli-status/pass] clang terminated by SIGILL is classified as signal death, not exit 132'
  set +e
  env TMPDIR="$signal_tmp" PATH="$FAKE_CLANG_DIR:$PATH" CLAW_TEST_CLANG_MODE=signal \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/signal.exe" >"$TEST_DIR/signal.stdout" 2>"$signal_stderr"
  signal_rc=$?
  set -e
  [[ "$signal_rc" -eq 1 ]] || fail "signal-terminated linker changed CLI failure exit code: $signal_rc"
  grep -Fq '`clang` terminated by signal 4.' "$signal_stderr" || fail "SIGILL was not classified as signal death"
  if grep -Fq '`clang` exited with status 132' "$signal_stderr"; then
    fail "signal-terminated clang was incorrectly classified as normal exit 132"
  fi
  assert_no_clang_diagnostics "$signal_tmp"

  injection_output="$TEST_DIR/output\"; touch \"$TEST_DIR/shell-injected\"; #.exe"
  printf '%s\n' '[cli-status/pass] linker arguments preserve spaces and shell metacharacters without shell execution'
  env PATH="$FAKE_CLANG_DIR:$PATH" CLAW_TEST_CLANG_MODE=success \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$injection_output" >"$TEST_DIR/injection.stdout" 2>"$TEST_DIR/injection.stderr" || fail "linker failed for a shell-sensitive output path"
  [[ -f "$injection_output" ]] || fail "linker output path containing shell characters was not preserved"
  [[ ! -e "$TEST_DIR/shell-injected" ]] || fail "shell metacharacters in linker arguments executed a command"
  [[ "$(cat "$TEST_DIR/injection.stdout")" == "Built native executable: $injection_output" ]] || fail "shell-sensitive output path changed in success output"

  large_tmp="$TEST_DIR/large-diagnostics-tmp"
  mkdir -p "$large_tmp"
  large_stderr="$TEST_DIR/large-diagnostics.stderr"
  printf '%s\n' '[cli-status/pass] large linker diagnostics are streamed intact and temporary files are removed'
  set +e
  env TMPDIR="$large_tmp" PATH="$FAKE_CLANG_DIR:$PATH" CLAW_TEST_CLANG_MODE=large-fail \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/large-failure.exe" >"$TEST_DIR/large-diagnostics.stdout" 2>"$large_stderr"
  large_rc=$?
  set -e
  [[ "$large_rc" -eq 1 ]] || fail "large-diagnostic linker failure exit code changed: $large_rc"
  python3 - "$large_stderr" <<'PY'
import sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
begin = b"LARGE-BEGIN\n"
end = b"\nLARGE-END\n"
start = raw.find(begin)
finish = raw.find(end, start + len(begin))
if start < 0 or finish < 0:
    raise SystemExit("large linker diagnostic markers were truncated")
payload = raw[start + len(begin):finish]
if payload != b"x" * (4 * 1024 * 1024):
    raise SystemExit(f"large linker diagnostic length/content changed: {len(payload)} bytes")
if b"`clang` exited with status 23" not in raw:
    raise SystemExit("large linker diagnostic omitted exit status classification")
PY
  assert_no_clang_diagnostics "$large_tmp"

  replaced_path_tmp="$TEST_DIR/replaced-diagnostics-path-tmp"
  mkdir -p "$replaced_path_tmp"
  replaced_path_stderr="$TEST_DIR/replaced-diagnostics-path.stderr"
  printf '%s\n' '[cli-status/pass] diagnostic replay uses the original open descriptor after its pathname is replaced'
  set +e
  env TMPDIR="$replaced_path_tmp" PATH="$FAKE_CLANG_DIR:$PATH" CLAW_TEST_CLANG_MODE=replace-diagnostics-path \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/replaced-path-failure.exe" >"$TEST_DIR/replaced-diagnostics-path.stdout" 2>"$replaced_path_stderr"
  replaced_path_rc=$?
  set -e
  [[ "$replaced_path_rc" -eq 1 ]] || fail "replaced diagnostic pathname changed CLI failure exit code: $replaced_path_rc"
  grep -Fq 'ORIGINAL-DESCRIPTOR-DIAGNOSTIC' "$replaced_path_stderr" || fail "diagnostic from original open descriptor was not replayed"
  if grep -Fq 'PATHNAME-REPLACEMENT-TRAP' "$replaced_path_stderr"; then
    fail "diagnostic replay reopened the replaced tempfile pathname"
  fi
  grep -Fq '`clang` exited with status 23' "$replaced_path_stderr" || fail "replaced diagnostic path omitted clang exit status"
  assert_no_clang_diagnostics "$replaced_path_tmp"
fi

if python3 -c 'import pty, tty' >/dev/null 2>&1; then
  pty_success_stderr="$TEST_DIR/pty-success.stderr"
  pty_success_stdout="$TEST_DIR/pty-success.stdout"
  pty_success_tmp="$TEST_DIR/pty-success-tmp"
  mkdir -p "$pty_success_tmp"
  printf '%s\n' '[cli-status/pass] PTY success uses ANSI color and an animated indeterminate phase'
  set +e
  env -u NO_COLOR TMPDIR="$pty_success_tmp" PATH="$FAKE_CLANG_DIR:$PATH" TERM=xterm-256color CLAW_TEST_CLANG_MODE=success \
    python3 "$SCRIPT_DIR/capture_pty.py" "$pty_success_stderr" "$pty_success_stdout" color -- \
    "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/pty-success.exe"
  pty_rc=$?
  set -e
  if [[ "$pty_rc" -eq 77 ]]; then
    printf '%s\n' '[cli-status/skip] PTY capture is unavailable on this platform'
  else
    [[ "$pty_rc" -eq 0 ]] || fail "PTY success build failed: $pty_rc"
    grep -Fq 'FORGE READY | native artifact ready' "$pty_success_stderr" || fail "PTY success status missing"
    grep -q $'\033\[' "$pty_success_stderr" || fail "interactive PTY output did not contain ANSI color"
    [[ "$(tr -cd '\r' <"$pty_success_stderr" | wc -c)" -ge 7 ]] || fail "spinner did not visibly animate during the slow linker phase"
    if grep -Eq '[0-9]+%' "$pty_success_stderr"; then
      fail "PTY output contains an estimated percentage"
    fi
    if [[ "$IS_POSIX" -eq 1 ]]; then
      assert_no_clang_diagnostics "$pty_success_tmp"
    fi

    pty_plain_stderr="$TEST_DIR/pty-no-color.stderr"
    pty_plain_stdout="$TEST_DIR/pty-no-color.stdout"
    printf '%s\n' '[cli-status/pass] NO_COLOR suppresses ANSI in a capable PTY'
    set +e
    PATH="$FAKE_CLANG_DIR:$PATH" TERM=xterm-256color CLAW_TEST_CLANG_MODE=success \
      python3 "$SCRIPT_DIR/capture_pty.py" "$pty_plain_stderr" "$pty_plain_stdout" no-color -- \
      "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/pty-no-color.exe"
    pty_plain_rc=$?
    set -e
    [[ "$pty_plain_rc" -eq 0 ]] || fail "NO_COLOR PTY build failed: $pty_plain_rc"
    if grep -q $'\033' "$pty_plain_stderr"; then
      fail "NO_COLOR did not suppress ANSI sequences in a PTY"
    fi
    grep -Fq 'FORGE READY | native artifact ready' "$pty_plain_stderr" || fail "NO_COLOR PTY success status missing"

    pty_dumb_stderr="$TEST_DIR/pty-term-dumb.stderr"
    pty_dumb_stdout="$TEST_DIR/pty-term-dumb.stdout"
    printf '%s\n' '[cli-status/pass] TERM=dumb suppresses ANSI in a PTY'
    set +e
    env -u NO_COLOR PATH="$FAKE_CLANG_DIR:$PATH" TERM=dumb CLAW_TEST_CLANG_MODE=success \
      python3 "$SCRIPT_DIR/capture_pty.py" "$pty_dumb_stderr" "$pty_dumb_stdout" color -- \
      "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/pty-term-dumb.exe"
    pty_dumb_rc=$?
    set -e
    [[ "$pty_dumb_rc" -eq 0 ]] || fail "TERM=dumb PTY build failed: $pty_dumb_rc"
    if grep -q $'\033' "$pty_dumb_stderr"; then
      fail "TERM=dumb did not suppress ANSI sequences in a PTY"
    fi
    grep -Fq 'FORGE READY | native artifact ready' "$pty_dumb_stderr" || fail "TERM=dumb PTY success status missing"

    pty_compile_failure_stderr="$TEST_DIR/pty-compile-failure.stderr"
    pty_compile_failure_stdout="$TEST_DIR/pty-compile-failure.stdout"
    printf '%s\n' '[cli-status/pass] PTY compile error keeps diagnostics and colors the failure status'
    set +e
    env -u NO_COLOR TERM=xterm-256color \
      python3 "$SCRIPT_DIR/capture_pty.py" "$pty_compile_failure_stderr" "$pty_compile_failure_stdout" color -- \
      "$CLAW_EXE" build "$FIXTURE_DIR/revise_bad_char_empty.cat" "$TEST_DIR/pty-compile-failure.exe"
    pty_compile_failure_rc=$?
    set -e
    [[ "$pty_compile_failure_rc" -eq 1 ]] || fail "PTY compile failure exit code changed: $pty_compile_failure_rc"
    grep -Fq 'error[parse]: Character literal must contain exactly one Unicode scalar value.' "$pty_compile_failure_stderr" || fail "PTY compile diagnostic missing"
    grep -Fq $'\033[31;1mFORGE HALTED | compile rejected; see diagnostics\033[0m' "$pty_compile_failure_stderr" || fail "PTY compile failure summary is not red"
    python3 - "$pty_compile_failure_stderr" <<'PY'
import re
import sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
text = raw.decode(errors="replace")
visible = re.sub(r"\x1b\[[0-9;]*m", "", text)
if not visible.rstrip().endswith("[=!....] | FORGE HALTED | compile rejected; see diagnostics"):
    raise SystemExit("final PTY line is not the settled compile failure status")
diagnostic_at = text.find("error[parse]: Character literal")
if diagnostic_at < 0 or not text[:diagnostic_at].endswith("\n"):
    raise SystemExit("compile diagnostic was interleaved with the progress line")
PY

    pty_failure_stderr="$TEST_DIR/pty-link-failure.stderr"
    pty_failure_stdout="$TEST_DIR/pty-link-failure.stdout"
    pty_failure_tmp="$TEST_DIR/pty-failure-tmp"
    mkdir -p "$pty_failure_tmp"
    printf '%s\n' '[cli-status/pass] PTY linker failure stops spinner, classifies exit 23, and cleans diagnostics'
    set +e
    env -u NO_COLOR TMPDIR="$pty_failure_tmp" PATH="$FAKE_CLANG_DIR:$PATH" TERM=xterm-256color CLAW_TEST_CLANG_MODE=fail \
      python3 "$SCRIPT_DIR/capture_pty.py" "$pty_failure_stderr" "$pty_failure_stdout" color -- \
      "$CLAW_EXE" build "$FIXTURE_DIR/revise_maybe.cat" "$TEST_DIR/pty-link-failure.exe"
    pty_failure_rc=$?
    set -e
    [[ "$pty_failure_rc" -eq 1 ]] || fail "PTY linker failure exit code changed: $pty_failure_rc"
    grep -Fq 'simulated clang failure: exit 23' "$pty_failure_stderr" || fail "simulated linker diagnostic missing"
    grep -Fq '`clang` exited with status 23' "$pty_failure_stderr" || fail "PTY linker failure omitted exit 23 classification"
    grep -q $'\033\[' "$pty_failure_stderr" || fail "interactive failure status did not use ANSI color"
    python3 - "$pty_failure_stderr" <<'PY'
import re
import sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
text = raw.decode(errors="replace")
visible = re.sub(r"\x1b\[[0-9;]*m", "", text)
if not visible.rstrip().endswith("FORGE HALTED | native build failed; see error above"):
    raise SystemExit("final PTY line is not the settled native-link failure status")
if "[=====!" not in visible:
    raise SystemExit("link failure did not mark the active native phase as failed")
diagnostic_at = text.find("simulated clang failure: exit 23")
if diagnostic_at < 0 or not text[:diagnostic_at].endswith("\n"):
    raise SystemExit("linker diagnostic was interleaved with the active spinner line")
if not raw.rstrip().endswith(b"\x1b[0m"):
    raise SystemExit("failure status did not reset its ANSI styling")
PY
    if [[ "$IS_POSIX" -eq 1 ]]; then
      assert_no_clang_diagnostics "$pty_failure_tmp"
    fi
  fi
else
  printf '%s\n' '[cli-status/skip] Python pseudo-terminal support is unavailable'
fi

printf '%s\n' 'All CLI status tests passed.'
