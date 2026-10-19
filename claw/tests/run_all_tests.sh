#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Running C@ PRD Doc-Test Suite (tagged slice) ==="
bash "$SCRIPT_DIR/docs/run_doc_tests.sh"

echo ""
echo "=== Running C@ Documentation Command CLI Test Suite ==="
python3 "$SCRIPT_DIR/docs/run_doc_command_tests.py"

echo ""
echo "=== Running C@ Native Path Regression Suite ==="
python3 "$SCRIPT_DIR/native/run_native_path_tests.py"

echo ""
echo "=== Running C@ Windows CRT Argument Regression Suite ==="
python3 "$SCRIPT_DIR/native/run_windows_argv_tests.py"

echo ""
echo "=== Running C@ Frontend Test Suite ==="
bash "$SCRIPT_DIR/frontend/run_frontend_tests.sh"

echo ""
echo "=== Running C@ Terminal Transport Contract Suite ==="
python3 "$SCRIPT_DIR/cli/run_terminal_contract_tests.py"

echo ""
echo "=== Running C@ Lexer Comment Regression Suite ==="
bash "$SCRIPT_DIR/frontend/run_lexer_comment_tests.sh"

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
echo "=== Running C@ PR25 Composition Integration Suite ==="
python3 "$SCRIPT_DIR/integration/run_composition_tests.py"
echo ""
echo "=== Running C@ Target-Directed Float Literal Regression Suite ==="
python3 "$SCRIPT_DIR/native/run_float_target_direct_tests.py"

echo ""
echo "=== Running C@ CLI Build Status Test Suite ==="
bash "$SCRIPT_DIR/cli/run_cli_status_tests.sh"

echo ""
echo "=== Running C@ CLI Diagnostic Format Suite ==="
bash "$SCRIPT_DIR/cli/run_diagnostic_format_tests.sh"

echo ""
echo "=== Running C@ CLI Build UX Contract Suite ==="
python3 "$SCRIPT_DIR/cli/run_build_ux_tests.py"

echo ""
echo "=== All C@ test suites passed successfully! ==="
