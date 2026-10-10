#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Running C@ Frontend Test Suite ==="
bash "$SCRIPT_DIR/frontend/run_frontend_tests.sh"

echo ""
echo "=== Running C@ OIR Formatter Golden Test Suite ==="
bash "$SCRIPT_DIR/oir/run_oir_tests.sh"

echo ""
echo "=== Running C@ Backend LLVM Test Suite ==="
bash "$SCRIPT_DIR/backend/run_backend_tests.sh"

echo ""
echo "=== Running C@ Native Executable Test Suite ==="
bash "$SCRIPT_DIR/native/run_native_tests.sh"

echo ""
echo "=== All C@ test suites passed successfully! ==="
