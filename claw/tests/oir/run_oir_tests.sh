#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
FIXTURE_DIR="$CLAW_DIR/tests"
GOLDEN_DIR="$SCRIPT_DIR/golden"

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

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

run_oir_golden() {
  local label="$1"
  local fixture="$2"
  local expected="$GOLDEN_DIR/$label.oir"
  local actual="$TEMP_DIR/$label.oir"

  echo "[oir/pass] $label"
  if ! "$CLAW_EXE" oir "$fixture" > "$actual"; then
    echo "OIR emission failed for $fixture" >&2
    exit 1
  fi
  if ! cmp -s "$expected" "$actual"; then
    echo "OIR output changed for $label (expected byte-for-byte match)" >&2
    diff -u --label "$expected" --label "$actual" "$expected" "$actual" || true
    exit 1
  fi
}

run_oir_golden "operator" "$FIXTURE_DIR/native/fixtures/revise_operator_overload.cat"
run_oir_golden "error" "$FIXTURE_DIR/frontend/fixtures/revise_error_handling.cat"
run_oir_golden "choice-resolution" "$FIXTURE_DIR/frontend/fixtures/revise_choice_resolution.cat"
run_oir_golden "anchor-choice" "$FIXTURE_DIR/native/fixtures/revise_anchor_choice.cat"
run_oir_golden "scope" "$FIXTURE_DIR/frontend/fixtures/revise_scope_refs.cat"
run_oir_golden "workspace" "$FIXTURE_DIR/frontend/fixtures/revise_nested_scoped_workspace"

echo "All OIR golden tests passed."
