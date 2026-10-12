#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"

python3 "$SCRIPT_DIR/run_doc_tests.py" "$REPO_DIR/cat-northstar/PRD.md"
