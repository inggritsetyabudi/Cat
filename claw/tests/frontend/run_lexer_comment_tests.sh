#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
if [[ -z "${CLAW_EXE:-}" ]]; then
  if [[ -x "$CLAW_DIR/build/claw" ]]; then
    CLAW_EXE="$CLAW_DIR/build/claw"
  else
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw.exe"
  fi
fi
python3 "$SCRIPT_DIR/run_lexer_comment_tests.py" "$CLAW_EXE"
