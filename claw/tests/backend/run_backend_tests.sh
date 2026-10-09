#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BACKEND_FIXTURE_DIR="$SCRIPT_DIR/fixtures"
FRONTEND_FIXTURE_DIR="$SCRIPT_DIR/../frontend/fixtures"

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

ARTIFACT_DIR="$SCRIPT_DIR/artifacts"
mkdir -p "$ARTIFACT_DIR"

run_llvm() {
  local label="$1"
  local input="$2"
  local output="$3"
  echo "[llvm/pass] $label"
  "$CLAW_EXE" llvm "$input" > "$output"
  if command -v llvm-as >/dev/null 2>&1; then
    llvm-as "$output" -o /dev/null
  else
    clang -x ir -c "$output" -o /dev/null
  fi
}

result_ll="$ARTIFACT_DIR/revise_result_llvm.ll"
maybe_ll="$ARTIFACT_DIR/revise_maybe.ll"
scope_ll="$ARTIFACT_DIR/revise_scope_refs.ll"
anchor_ll="$ARTIFACT_DIR/revise_anchor.ll"
anchor_choice_ll="$ARTIFACT_DIR/revise_anchor_choice.ll"
view_shape_ll="$ARTIFACT_DIR/revise_view_shape_scope.ll"
scoped_type_ll="$ARTIFACT_DIR/revise_scoped_type_propagation.ll"
nested_scoped_ll="$ARTIFACT_DIR/revise_nested_scoped_generic.ll"
nested_scoped_workspace_ll="$ARTIFACT_DIR/revise_nested_scoped_workspace.ll"
str_search_ll="$ARTIFACT_DIR/revise_str_search_methods.ll"
rm -f "$result_ll" "$maybe_ll" "$scope_ll" "$anchor_ll" "$anchor_choice_ll" "$view_shape_ll" "$scoped_type_ll" "$nested_scoped_ll" "$nested_scoped_workspace_ll" "$str_search_ll"

run_llvm "test_backend/revise_result_llvm.cat" "$BACKEND_FIXTURE_DIR/revise_result_llvm.cat" "$result_ll"
llvm_output="$(tr -d '\r' < "$result_ll")"
if [[ "$llvm_output" != *'define internal void @"revise_result_llvm::step"'* ]] ||
   [[ "$llvm_output" != *'define internal void @"revise_result_llvm::show"'* ]] ||
   [[ "$llvm_output" != *'@"claw.runtime.println.i32"'* ]] ||
   [[ "$llvm_output" != *'@"claw.runtime.println.slice"'* ]] ||
   [[ "$llvm_output" != *'try_fail_0:'* ]]; then
  echo "revised backend LLVM output did not include the expected Result/try lowering markers" >&2
  exit 1
fi

run_llvm "test_frontend/revise_maybe.cat" "$FRONTEND_FIXTURE_DIR/revise_maybe.cat" "$maybe_ll"

run_llvm "test_backend/revise_scope_refs.cat" "$BACKEND_FIXTURE_DIR/revise_scope_refs.cat" "$scope_ll"
scope_output="$(tr -d '\r' < "$scope_ll")"
if [[ "$scope_output" != *'define internal void @"revise_scope_refs::main"'* ]] ||
   [[ "$scope_output" != *'@"claw.runtime.println.slice"'* ]] ||
   [[ "$scope_output" != *'scope_s_0:'* ]]; then
  echo "revised scope-ref LLVM output did not include the expected lowering markers" >&2
  exit 1
fi

run_llvm "test_backend/revise_anchor.cat" "$BACKEND_FIXTURE_DIR/revise_anchor.cat" "$anchor_ll"
anchor_output="$(tr -d '\r' < "$anchor_ll")"
if [[ "$anchor_output" != *'@"claw.runtime.anchor.alloc"'* ]] ||
   [[ "$anchor_output" != *'@"claw.runtime.anchor.free"'* ]] ||
   [[ "$anchor_output" != *'@"claw.runtime.println.slice"'* ]]; then
  echo "revised anchor LLVM output did not include the expected anchor lowering markers" >&2
  exit 1
fi

run_llvm "test_backend/revise_anchor_choice.cat" "$BACKEND_FIXTURE_DIR/revise_anchor_choice.cat" "$anchor_choice_ll"
anchor_choice_output="$(tr -d '\r' < "$anchor_choice_ll")"
if [[ "$anchor_choice_output" != *'@"claw.runtime.anchor.alloc"'* ]] ||
   [[ "$anchor_choice_output" != *'@"claw.runtime.anchor.free"'* ]] ||
   [[ "$anchor_choice_output" != *'switch i32'* ]]; then
  echo "revised anchor-choice LLVM output did not include the expected choice + anchor lowering markers" >&2
  exit 1
fi

run_llvm "test_backend/revise_view_shape_scope.cat" "$BACKEND_FIXTURE_DIR/revise_view_shape_scope.cat" "$view_shape_ll"
view_shape_output="$(tr -d '\r' < "$view_shape_ll")"
if [[ "$view_shape_output" != *'define internal void @"revise_view_shape_scope::main"'* ]] ||
   [[ "$view_shape_output" != *'@"claw.runtime.println.slice"'* ]]; then
  echo "revised view-shape LLVM output did not include the expected lowering markers" >&2
  exit 1
fi

run_llvm "test_frontend/revise_scoped_type_propagation.cat" "$FRONTEND_FIXTURE_DIR/revise_scoped_type_propagation.cat" "$scoped_type_ll"
scoped_type_output="$(tr -d '\r' < "$scoped_type_ll")"
if [[ "$scoped_type_output" != *'call %claw.slice @"revise_scoped_type_propagation::identity_ref"('* ]] ||
   [[ "$scoped_type_output" != *'call %"revise_scoped_type_propagation::PairView" @"revise_scoped_type_propagation::identity_pair"('* ]] ||
   [[ "$scoped_type_output" != *'call %claw.slice @"revise_scoped_type_propagation::first_text"('* ]] ||
   [[ "$scoped_type_output" != *'@"claw.runtime.println.slice"'* ]]; then
  echo "scoped-type LLVM output did not include signature propagation markers" >&2
  exit 1
fi

run_llvm "test_frontend/revise_nested_scoped_generic.cat" "$FRONTEND_FIXTURE_DIR/revise_nested_scoped_generic.cat" "$nested_scoped_ll"
nested_scoped_output="$(tr -d '\r' < "$nested_scoped_ll")"
if [[ "$nested_scoped_output" != *'define internal void @"revise_nested_scoped_generic::identity_box"(ptr %ret.slot, ptr %arg.value.addr)'* ]] ||
   [[ "$nested_scoped_output" != *'call void @"revise_nested_scoped_generic::identity_box"(ptr '* ]] ||
   [[ "$nested_scoped_output" != *'call %claw.slice @"revise_nested_scoped_generic::first_text"(ptr '* ]] ||
   [[ "$nested_scoped_output" != *'alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16'* ]] ||
   [[ "$nested_scoped_output" != *'getelementptr inbounds { %"revise_nested_scoped_generic::Name", i128 }'* ]]; then
  echo "nested scoped generic LLVM output did not include instantiated layout, indirect ABI, calls, and field-lowering markers" >&2
  exit 1
fi

run_llvm "test_frontend/revise_nested_scoped_workspace" "$FRONTEND_FIXTURE_DIR/revise_nested_scoped_workspace" "$nested_scoped_workspace_ll"

run_llvm "test_frontend/revise_str_search_methods.cat" "$FRONTEND_FIXTURE_DIR/revise_str_search_methods.cat" "$str_search_ll"
str_search_output="$(tr -d '\r' < "$str_search_ll")"
if [[ "$str_search_output" != *'call i1 @"claw.runtime.str.starts_with"(ptr '* ]] ||
   [[ "$str_search_output" != *'call i1 @"claw.runtime.str.ends_with"(ptr '* ]] ||
   [[ "$str_search_output" != *'call i1 @"claw.runtime.str.contains"(ptr '* ]]; then
  echo "Str search LLVM output did not include receiver-first runtime calls" >&2
  exit 1
fi
