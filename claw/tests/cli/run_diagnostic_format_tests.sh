#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/claw-diagnostic-format.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-clang++}" -std=c++20 -I"$CLAW_DIR/src" \
  "$SCRIPT_DIR/fixtures/diagnostic_format.cpp" \
  "$CLAW_DIR/src/diagnostics/diagnostics.cpp" -o "$TEST_DIR/diagnostic-format.exe"
"$TEST_DIR/diagnostic-format.exe"
