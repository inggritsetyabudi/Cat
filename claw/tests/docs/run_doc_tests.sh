#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"

python3 "$SCRIPT_DIR/validate_feature_status.py" --root "$REPO_DIR"
# Includes mini-repository subprocess E2E coverage as well as validator unit tests.
python3 -m unittest discover -s "$SCRIPT_DIR" -p 'test_feature_status*.py' -v
python3 "$SCRIPT_DIR/run_doc_tests.py" "$REPO_DIR/cat-northstar/PRD.md"
