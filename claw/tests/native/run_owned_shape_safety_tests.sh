#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
FIXTURE_DIR="$SCRIPT_DIR/owned_shape"
ARTIFACT_DIR="$SCRIPT_DIR/artifacts/owned_shape"
RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/claw-owned-shape.XXXXXX")"
trap 'rm -rf -- "$RUN_DIR"' EXIT
# The parent supplies its exact compiler as an argument across shell boundaries.
# Keep the environment/default fallback for standalone invocations only.
CLAW_EXE="${1:-${CLAW_EXE:-$CLAW_DIR/build/claw}}"
[[ -x "$CLAW_EXE" ]] || { echo "missing compiler executable: $CLAW_EXE" >&2; exit 1; }
mkdir -p "$ARTIFACT_DIR"
PROJECTION='Cannot move an owned shape field through projection; partial moves are not supported. Hint: borrow the field, call `clone()` for an explicit copy, or move the complete shape.'
MOVED='Use of moved value -> self. Hint: use the value before moving it, or call `clone()` when a separate owned copy is required.'
normalize() { tr -d '\r'; }
expect_no_outputs() { [[ ! -e "$1" && ! -e "${1%.exe}.ll" ]] || { echo "unexpected native artifact for rejected fixture: $1" >&2; exit 1; }; }
run_negative() {
  local name="$1" diag="$2" src="$FIXTURE_DIR/$1.cat"
  [[ -f "$src" ]] || { echo "missing negative fixture: $src" >&2; exit 1; }
  for mode in check oir llvm; do
    local log="$RUN_DIR/$name.$mode.log"
    if "$CLAW_EXE" "$mode" "$src" >"$RUN_DIR/$name.$mode.stdout" 2>"$log"; then echo "$mode unexpectedly accepted $name" >&2; exit 1; fi
    [[ ! -s "$RUN_DIR/$name.$mode.stdout" ]] || { echo "unexpected stdout for $name/$mode" >&2; exit 1; }
    [[ "$(grep -Foc "$diag" "$log" || true)" == 1 && "$(grep -Ec '^error\[' "$log" || true)" == 1 ]] || { echo "non-exact diagnostic for $name/$mode" >&2; cat "$log" >&2; exit 1; }
  done
  local out="$ARTIFACT_DIR/$name.exe"; rm -f "$out" "${out%.exe}.ll"
  if "$CLAW_EXE" build "$src" "$out" >"$RUN_DIR/$name.build.log" 2>&1; then echo "build unexpectedly accepted $name" >&2; exit 1; fi
  build_errors="$(grep -Ec '^error: ' "$RUN_DIR/$name.build.log" || true)"
  build_failure="$(grep -Foc "error: build failed for \`$name\`" "$RUN_DIR/$name.build.log" || true)"
  [[ "$(grep -Foc "$diag" "$RUN_DIR/$name.build.log" || true)" == 1 && "$build_errors" == 2 && "$build_failure" == 1 ]] || { echo "non-exact diagnostic for $name/build" >&2; cat "$RUN_DIR/$name.build.log" >&2; exit 1; }
  expect_no_outputs "$out"
  echo "[check/oir/llvm/build/pass] $name"
}
projection_negatives=(multi_owned_first multi_owned_middle multi_owned_last one_owned mixed_plain_owned nested_local chained_local temporary_chain initializer assignment argument return_value if_branch pick_branch loop_body remaining_field owned_receiver method_body_projection view_shape_owned method_tail_projection_negative method_explicit_return_projection_negative static_constructor_projection_negative operator_owned_lhs_negative operator_owned_rhs_negative)
for name in "${projection_negatives[@]}"; do run_negative "$name" "$PROJECTION"; done
run_negative method_receiver_moved_use_negative "$MOVED"
run_negative owned_receiver_caller_moved_use_negative 'Use of moved value -> source. Hint: use the value before moving it, or call `clone()` when a separate owned copy is required.'
run_negative operator_borrow_move_conflict_negative $'Cannot move `root` while `root` is still borrowed. Hint: end the view\'s scope before moving or replacing its owner.'

# Every positive is checked through semantic analysis, OIR, LLVM, native link/run.
declare -A EXPECTED=(
  [owned_shapes_positive]=$'first\nsecond\nthird\nfirst\nsecond\nthird\nsecond\nfirst\nthird\nsecond\ninner\nouter\nparam\nparam\nbranch\nthird\nthird\nsecond\nthird\nfirst\nsecond\nnew\nkept'
  [whole_shape_receiver_positive]=$'left'
  [alias_borrowed_owner]=$'left\nright'
  [ref_view_nested]=$'left'
  [owned_receiver_transfer_positive]=$'temporary'
  [multi_param_method_positive]=$'chosen'
  [operator_owned_full_positive]=$'left'
  [operator_borrowed_same_root_positive]=$'root'
)
declare -A CLASSIFIED=()
for name in "${projection_negatives[@]}" method_receiver_moved_use_negative owned_receiver_caller_moved_use_negative operator_borrow_move_conflict_negative "${!EXPECTED[@]}"; do
  [[ -z "${CLASSIFIED[$name]:-}" ]] || { echo "duplicate fixture classification: $name" >&2; exit 1; }
  CLASSIFIED[$name]=1
done
for src in "$FIXTURE_DIR"/*.cat; do
  name="$(basename "$src" .cat)"
  [[ -n "${CLASSIFIED[$name]:-}" ]] || { echo "unclassified fixture: $name" >&2; exit 1; }
done
for name in "${!EXPECTED[@]}"; do
  src="$FIXTURE_DIR/$name.cat"; out="$ARTIFACT_DIR/$name.exe"; ll="${out%.exe}.ll"
  [[ -f "$src" ]] || { echo "missing positive fixture: $src" >&2; exit 1; }
  for mode in check oir llvm; do "$CLAW_EXE" "$mode" "$src" >"$RUN_DIR/$name.$mode.log" 2>&1; done
  rm -f "$out" "$ll"; "$CLAW_EXE" build "$src" "$out" >"$RUN_DIR/$name.build.log" 2>&1
  [[ -x "$out" && -s "$ll" ]] || { echo "positive build artifacts missing: $name" >&2; exit 1; }
  actual="$($out | normalize)"; [[ "$actual" == "${EXPECTED[$name]}" ]] || { echo "stdout mismatch: $name" >&2; printf '%s\n' "$actual" >&2; exit 1; }
  echo "[check/oir/llvm/build/run/pass] $name"
done

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) echo "[sanitizer/skip] Windows sanitizer runtime is explicitly skipped"; exit 0 ;;
esac
if ! printf 'int main(void) { return 0; }\n' | clang -fsanitize=address,undefined,leak -x c - -o "$RUN_DIR/probe" >/dev/null 2>&1; then
  [[ "${CLAW_REQUIRE_SANITIZERS:-0}" != 1 ]] || { echo "CLAW_REQUIRE_SANITIZERS=1 but sanitizer runtime is unavailable" >&2; exit 1; }
  echo "[sanitizer/skip] ASan/UBSan/LSan runtime unavailable"; exit 0
fi
for name in "${!EXPECTED[@]}"; do
  ll="$ARTIFACT_DIR/$name.ll"
  for opt in 0 1; do
    san="$RUN_DIR/$name.sanO$opt"
    clang -Wno-override-module -fsanitize=address,undefined,leak -fno-omit-frame-pointer -O"$opt" -g "$ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$san"
    actual="$(ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 LSAN_OPTIONS=exitcode=23 "$san" | normalize)"
    [[ "$actual" == "${EXPECTED[$name]}" ]] || { echo "sanitized stdout mismatch: $name O$opt" >&2; exit 1; }
  done
done
echo "[asan/ubsan/lsan/O0/O1/pass] ${#EXPECTED[@]} positives"
echo "owned-shape safety tests passed: ${#projection_negatives[@]} projection negatives + 2 moved-use negatives + 1 borrow-conflict negative; ${#EXPECTED[@]} positives"
