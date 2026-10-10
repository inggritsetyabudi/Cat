#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
FRONTEND_FIXTURE_DIR="$SCRIPT_DIR/../frontend/fixtures"
NATIVE_FIXTURE_DIR="$SCRIPT_DIR/fixtures"

if [[ -z "${CLAW_EXE:-}" ]]; then
  if [[ -x "$CLAW_DIR/build/claw" ]]; then
    CLAW_EXE="$CLAW_DIR/build/claw"
  elif [[ -x "$CLAW_DIR/build-ucrt64-clang/claw.exe" ]]; then
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw.exe"
  else
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw-codex.exe"
  fi
fi

export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  exit 1
fi

CLAW_EXE_DIR="$(cd -- "$(dirname -- "$CLAW_EXE")" && pwd)"
CLAW_EXE="$CLAW_EXE_DIR/$(basename -- "$CLAW_EXE")"

ARTIFACT_DIR="$SCRIPT_DIR/artifacts"
mkdir -p "$ARTIFACT_DIR"
CWD_TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/claw-native-cwd.XXXXXX")"
trap 'rm -rf "$CWD_TEST_DIR"' EXIT

normalize_stdout() {
  "$1" | tr -d '\r'
}

expect_generated_ll() {
  local exe_path="$1"
  local ll_path="${exe_path%.exe}.ll"

  if [[ ! -f "$ll_path" ]]; then
    echo "expected LLVM IR next to built executable: $ll_path" >&2
    exit 1
  fi

  if ! grep -q 'define i32 @main()' "$ll_path"; then
    echo "generated LLVM IR did not expose the native @main entry wrapper: $ll_path" >&2
    exit 1
  fi
}

single_output="$ARTIFACT_DIR/revise_single_file.exe"
numeric_literals_output="$ARTIFACT_DIR/revise_numeric_literals.exe"
choice_resolution_output="$ARTIFACT_DIR/revise_choice_resolution.exe"
workspace_output="$ARTIFACT_DIR/revise_workspace.exe"
maybe_output="$ARTIFACT_DIR/revise_maybe.exe"
exit_code_output="$ARTIFACT_DIR/revise_exit_code.exe"
scope_output="$ARTIFACT_DIR/revise_scope_refs.exe"
anchor_output="$ARTIFACT_DIR/revise_anchor.exe"
anchor_choice_output="$ARTIFACT_DIR/revise_anchor_choice.exe"
view_shape_output="$ARTIFACT_DIR/revise_view_shape_scope.exe"
scoped_type_output="$ARTIFACT_DIR/revise_scoped_type_propagation.exe"
scoped_workspace_output="$ARTIFACT_DIR/revise_scoped_workspace.exe"
nested_scoped_output="$ARTIFACT_DIR/revise_nested_scoped_generic.exe"
nested_scoped_workspace_output="$ARTIFACT_DIR/revise_nested_scoped_workspace.exe"

rm -f "$single_output" "${single_output%.exe}.ll" \
      "$numeric_literals_output" "${numeric_literals_output%.exe}.ll" \
      "$choice_resolution_output" "${choice_resolution_output%.exe}.ll" \
      "$workspace_output" "${workspace_output%.exe}.ll" \
      "$maybe_output" "${maybe_output%.exe}.ll" \
      "$exit_code_output" "${exit_code_output%.exe}.ll" \
      "$scope_output" "${scope_output%.exe}.ll" \
      "$anchor_output" "${anchor_output%.exe}.ll" \
      "$anchor_choice_output" "${anchor_choice_output%.exe}.ll" \
      "$view_shape_output" "${view_shape_output%.exe}.ll" \
      "$scoped_type_output" "${scoped_type_output%.exe}.ll" \
      "$scoped_workspace_output" "${scoped_workspace_output%.exe}.ll" \
      "$nested_scoped_output" "${nested_scoped_output%.exe}.ll" \
      "$nested_scoped_workspace_output" "${nested_scoped_workspace_output%.exe}.ll"

echo "[build/pass] test_native/revise_single_file.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_single_file.cat" "$single_output" >/dev/null
expect_generated_ll "$single_output"
single_stdout="$(normalize_stdout "$single_output")"
if [[ "$single_stdout" != "3" ]]; then
  echo "revised single-file native build produced unexpected output: $single_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_numeric_literals.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_numeric_literals.cat" "$numeric_literals_output" >/dev/null
expect_generated_ll "$numeric_literals_output"
numeric_literals_stdout="$(normalize_stdout "$numeric_literals_output")"
if [[ "$numeric_literals_stdout" != "127" ]]; then
  echo "numeric-literal native build produced unexpected output: $numeric_literals_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_choice_resolution.cat"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_choice_resolution.cat" "$choice_resolution_output" >/dev/null
expect_generated_ll "$choice_resolution_output"
choice_resolution_stdout="$(normalize_stdout "$choice_resolution_output")"
if [[ "$choice_resolution_stdout" != $'42\n42' ]]; then
  echo "Choice-resolution native build produced unexpected output: $choice_resolution_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_workspace"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_workspace" "$workspace_output" >/dev/null
expect_generated_ll "$workspace_output"
workspace_stdout="$(normalize_stdout "$workspace_output")"
if [[ "$workspace_stdout" != "36" ]]; then
  echo "revised workspace native build produced unexpected output: $workspace_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_maybe.cat"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_maybe.cat" "$maybe_output" >/dev/null
expect_generated_ll "$maybe_output"
maybe_stdout="$(normalize_stdout "$maybe_output")"
expected_maybe_stdout=$'42\n0\nC@\nworld'
if [[ "$maybe_stdout" != "$expected_maybe_stdout" ]]; then
  echo "revised Maybe native build produced unexpected output:" >&2
  printf '%s\n' "$maybe_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_scope_refs.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_scope_refs.cat" "$scope_output" >/dev/null
expect_generated_ll "$scope_output"
scope_stdout="$(normalize_stdout "$scope_output")"
if [[ "$scope_stdout" != "scope" ]]; then
  echo "revised scope-ref native build produced unexpected output: $scope_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_anchor.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_anchor.cat" "$anchor_output" >/dev/null
expect_generated_ll "$anchor_output"
anchor_stdout="$(normalize_stdout "$anchor_output")"
if [[ "$anchor_stdout" != "anchor" ]]; then
  echo "revised anchor native build produced unexpected output: $anchor_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_anchor_choice.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_anchor_choice.cat" "$anchor_choice_output" >/dev/null
expect_generated_ll "$anchor_choice_output"
anchor_choice_stdout="$(normalize_stdout "$anchor_choice_output")"
expected_anchor_choice_stdout=$'choice\nnone'
if [[ "$anchor_choice_stdout" != "$expected_anchor_choice_stdout" ]]; then
  echo "revised anchor-choice native build produced unexpected output:" >&2
  printf '%s\n' "$anchor_choice_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_view_shape_scope.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_view_shape_scope.cat" "$view_shape_output" >/dev/null
expect_generated_ll "$view_shape_output"
view_shape_stdout="$(normalize_stdout "$view_shape_output")"
if [[ "$view_shape_stdout" != "hello" ]]; then
  echo "revised view-shape native build produced unexpected output: $view_shape_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_scoped_type_propagation.cat"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_scoped_type_propagation.cat" "$scoped_type_output" >/dev/null
expect_generated_ll "$scoped_type_output"
scoped_type_stdout="$(normalize_stdout "$scoped_type_output")"
if [[ "$scoped_type_stdout" != "alpha" ]]; then
  echo "scoped-type native build produced unexpected output: $scoped_type_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_scoped_workspace"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_scoped_workspace" "$scoped_workspace_output" >/dev/null
expect_generated_ll "$scoped_workspace_output"
scoped_workspace_stdout="$(normalize_stdout "$scoped_workspace_output")"
if [[ "$scoped_workspace_stdout" != "workspace" ]]; then
  echo "scoped workspace native build produced unexpected output: $scoped_workspace_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_nested_scoped_generic.cat"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_nested_scoped_generic.cat" "$nested_scoped_output" >/dev/null
expect_generated_ll "$nested_scoped_output"
nested_scoped_stdout="$(normalize_stdout "$nested_scoped_output")"
if [[ "$nested_scoped_stdout" != "alpha" ]]; then
  echo "nested scoped generic native build produced unexpected output: $nested_scoped_stdout" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_nested_scoped_workspace"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_nested_scoped_workspace" "$nested_scoped_workspace_output" >/dev/null
expect_generated_ll "$nested_scoped_workspace_output"
nested_scoped_workspace_stdout="$(normalize_stdout "$nested_scoped_workspace_output")"
if [[ "$nested_scoped_workspace_stdout" != "workspace" ]]; then
  echo "nested scoped generic workspace native build produced unexpected output: $nested_scoped_workspace_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_builtin_methods.cat"
builtin_methods_output="$ARTIFACT_DIR/revise_builtin_methods.exe"
rm -f "$builtin_methods_output"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_builtin_methods.cat" "$builtin_methods_output" >/dev/null
expect_generated_ll "$builtin_methods_output"
"$builtin_methods_output" >/dev/null 2>&1 || exit_code="$?"
exit_code="${exit_code:-0}"
if [[ "$exit_code" != "0" ]]; then
  echo "revised builtin-methods native build produced unexpected exit code: $exit_code" >&2
  exit 1
fi

echo "[build/pass] test_frontend/revise_str_search_methods.cat"
str_search_output="$ARTIFACT_DIR/revise_str_search_methods.exe"
rm -f "$str_search_output" "${str_search_output%.exe}.ll"
"$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_str_search_methods.cat" "$str_search_output" >/dev/null
expect_generated_ll "$str_search_output"
str_search_stdout="$(normalize_stdout "$str_search_output")"
if [[ "$str_search_stdout" != "str-search-ok" ]]; then
  echo "Str search native output was unexpected: $str_search_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_implements.cat"
implements_output="$ARTIFACT_DIR/revise_implements.exe"
rm -f "$implements_output" "${implements_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_implements.cat" "$implements_output" >/dev/null
expect_generated_ll "$implements_output"
implements_stdout="$(normalize_stdout "$implements_output")"
if [[ "$implements_stdout" != "localhost" ]]; then
  echo "revised implements native output was unexpected: $implements_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_implements_advanced.cat"
implements_adv_output="$ARTIFACT_DIR/revise_implements_advanced.exe"
rm -f "$implements_adv_output" "${implements_adv_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_implements_advanced.cat" "$implements_adv_output" >/dev/null
expect_generated_ll "$implements_adv_output"
implements_adv_stdout="$(normalize_stdout "$implements_adv_output")"
if [[ "$implements_adv_stdout" != "24" ]]; then
  echo "revised implements advanced native output was unexpected: $implements_adv_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_static.cat"
static_output="$ARTIFACT_DIR/revise_static.exe"
rm -f "$static_output" "${static_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_static.cat" "$static_output" >/dev/null
expect_generated_ll "$static_output"
static_stdout="$(normalize_stdout "$static_output")"
if [[ "$static_stdout" != $'1.0.0\ntrue' ]]; then
  echo "revised static native output was unexpected: $static_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_contract.cat"
contract_output="$ARTIFACT_DIR/revise_contract.exe"
rm -f "$contract_output" "${contract_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_contract.cat" "$contract_output" >/dev/null
expect_generated_ll "$contract_output"
contract_stdout="$(normalize_stdout "$contract_output")"
if [[ "$contract_stdout" != $'30\n40' ]]; then
  echo "revised contract native output was unexpected: $contract_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_arena.cat"
arena_output="$ARTIFACT_DIR/revise_arena.exe"
rm -f "$arena_output" "${arena_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_arena.cat" "$arena_output" >/dev/null
expect_generated_ll "$arena_output"
arena_stdout="$(normalize_stdout "$arena_output")"
if [[ "$arena_stdout" != $'100\n200\n300' ]]; then
  echo "revised arena native output was unexpected: $arena_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_operator_overload.cat"
op_output="$ARTIFACT_DIR/revise_operator_overload.exe"
rm -f "$op_output" "${op_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_operator_overload.cat" "$op_output" >/dev/null
expect_generated_ll "$op_output"
op_stdout="$(normalize_stdout "$op_output")"
if [[ "$op_stdout" != $'15\n35\nfalse\ntrue\n17\n38' ]]; then
  echo "revised operator overload native output was unexpected: $op_stdout" >&2
  exit 1
fi

echo "[build/pass] compiler locates bundled runtime outside repository working directory"
(
  cd -- "$CWD_TEST_DIR"
  cwd_independent_output="$CWD_TEST_DIR/revise_cwd_independent.exe"
  "$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_single_file.cat" "$cwd_independent_output" >/dev/null
  expect_generated_ll "$cwd_independent_output"
  cwd_independent_stdout="$(normalize_stdout "$cwd_independent_output")"
  if [[ "$cwd_independent_stdout" != "3" ]]; then
    echo "native build from an unrelated working directory produced unexpected output: $cwd_independent_stdout" >&2
    exit 1
  fi
)

echo "[build/pass] test_native/revise_exit_code.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_exit_code.cat" "$exit_code_output" >/dev/null
expect_generated_ll "$exit_code_output"
"$exit_code_output" >/dev/null 2>&1 || exit_code="$?"
exit_code="${exit_code:-0}"
if [[ "$exit_code" != "7" ]]; then
  echo "revised exit-code native build produced unexpected process exit code: $exit_code" >&2
  exit 1
fi
