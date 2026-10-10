#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
FIXTURE_DIR="$SCRIPT_DIR/fixtures"

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

run_check_pass() {
  local label="$1"
  local path="$2"
  echo "[check/pass] $label"
  "$CLAW_EXE" check "$path"
}

run_validate_pass() {
  local label="$1"
  local path="$2"
  echo "[validate/pass] $label"
  "$CLAW_EXE" validate "$path"
}

run_check_fail() {
  local label="$1"
  local path="$2"
  local expect="$3"
  echo "[check/fail] $label"
  local output
  if output="$("$CLAW_EXE" check "$path" 2>&1)"; then
    echo "expected $label to fail, but it passed" >&2
    exit 1
  fi
  if [[ "$output" != *"$expect"* ]]; then
    echo "$label did not report expected diagnostic substring: $expect" >&2
    echo "$output" >&2
    exit 1
  fi
}

run_validate_fail() {
  local label="$1"
  local path="$2"
  local expect="$3"
  echo "[validate/fail] $label"
  local output
  if output="$("$CLAW_EXE" validate "$path" 2>&1)"; then
    echo "expected $label to fail validation, but it passed" >&2
    exit 1
  fi
  if [[ "$output" != *"$expect"* ]]; then
    echo "$label did not report expected diagnostic substring: $expect" >&2
    echo "$output" >&2
    exit 1
  fi
}

echo "[check/pass] test_frontend/revise_surface.cat"
"$CLAW_EXE" check "$FIXTURE_DIR/revise_surface.cat"

echo "[air/pass] test_frontend/revise_surface.cat"
revise_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_surface.cat")"
if [[ "$revise_air_output" != *"air.module revise_surface"* ]] ||
   [[ "$revise_air_output" != *"val greeting: Str"* ]] ||
   [[ "$revise_air_output" != *"var total: Int64 = 42 : Int64"* ]] ||
   [[ "$revise_air_output" != *"scan item: UInt8 over greeting : Str"* ]]; then
  echo "revised surface AIR did not reflect the expected val/var and Str surface" >&2
  exit 1
fi

run_validate_pass "test_frontend/revise_pkg" "$FIXTURE_DIR/revise_pkg"
run_check_pass "test_frontend/revise_pkg" "$FIXTURE_DIR/revise_pkg"
run_validate_fail "test_frontend/revise_bad_main_return" "$FIXTURE_DIR/revise_bad_main_return" '`main` must take no parameters and return Unit or Int32.'
run_check_fail "test_frontend/revise_bad_double_main.cat" "$FIXTURE_DIR/revise_bad_double_main.cat" 'main.cat must contain exactly one `fn main` declaration.'
run_validate_fail "test_frontend/revise_bad_non_entry_main" "$FIXTURE_DIR/revise_bad_non_entry_main" '`fn main` is only allowed in root main.cat.'
run_check_fail "test_frontend/revise_bad_call_main.cat" "$FIXTURE_DIR/revise_bad_call_main.cat" '`main` is the program entry point and cannot be called like a normal function.'

run_check_pass "test_frontend/revise_builtin_methods.cat" "$FIXTURE_DIR/revise_builtin_methods.cat"
run_check_pass "test_frontend/revise_builtin_method_views.cat" "$FIXTURE_DIR/revise_builtin_method_views.cat"
run_check_fail "test_frontend/revise_bad_builtin_method_receiver.cat" "$FIXTURE_DIR/revise_bad_builtin_method_receiver.cat" 'Method receiver type mismatch: expected ref mut Vec[Int32], got Vec[Int32]'
run_check_pass "test_frontend/revise_str_search_methods.cat" "$FIXTURE_DIR/revise_str_search_methods.cat"
run_check_pass "test_frontend/revise_choice_resolution.cat" "$FIXTURE_DIR/revise_choice_resolution.cat"
echo "[air/golden] test_frontend/revise_choice_resolution.cat"
choice_resolution_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_choice_resolution.cat")"
if ! diff -u "$FIXTURE_DIR/revise_choice_resolution.air.golden" <(printf '%s\n' "$choice_resolution_air_output"); then
  echo "Choice-resolution AIR output differs from the golden" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_result_choice_resolution.cat" "$FIXTURE_DIR/revise_bad_result_choice_resolution.cat" '`try` requires Result to define Ok(T) and Fail(E) single-payload variants.'
run_check_pass "test_frontend/revise_numeric_literals.cat" "$FIXTURE_DIR/revise_numeric_literals.cat"
echo "[air/golden] test_frontend/revise_numeric_literals.cat"
numeric_literals_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_numeric_literals.cat")"
if ! diff -u "$FIXTURE_DIR/revise_numeric_literals.air.golden" <(printf '%s\n' "$numeric_literals_air_output"); then
  echo "numeric-literal AIR output differs from the baseline golden" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_numeric_literal.cat" "$FIXTURE_DIR/revise_bad_numeric_literal.cat" "Integer literal '340282366920938463463374607431768211456' does not fit target type UInt128."
run_check_fail "test_frontend/revise_bad_str_search_argument.cat" "$FIXTURE_DIR/revise_bad_str_search_argument.cat" 'Call argument type mismatch: expected ref Str, got integer literal'
run_check_fail "test_frontend/revise_bad_str_search_receiver.cat" "$FIXTURE_DIR/revise_bad_str_search_receiver.cat" "Type 'Int32' does not provide method 'starts_with'."

run_check_pass "test_frontend/revise_error_handling.cat" "$FIXTURE_DIR/revise_error_handling.cat"
echo "[air/pass] test_frontend/revise_error_handling.cat"
revise_try_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_error_handling.cat")"
if [[ "$revise_try_air_output" != *"val value = try step("* ]] ||
   [[ "$revise_try_air_output" != *"else err"* ]] ||
   [[ "$revise_try_air_output" != *"return Ok("* ]]; then
  echo "revised error-handling AIR did not reflect the expected try/Result surface" >&2
  exit 1
fi

run_check_fail "test_frontend/revise_bad_try_return.cat" "$FIXTURE_DIR/revise_bad_try_return.cat" '`try` shorthand requires the current function to return Result[T, E].'
run_check_fail "test_frontend/revise_bad_try_var.cat" "$FIXTURE_DIR/revise_bad_try_var.cat" '`try` bindings currently require `val`.'
run_check_fail "test_frontend/revise_bad_unused_result.cat" "$FIXTURE_DIR/revise_bad_unused_result.cat" 'Unused Result['
run_check_fail "test_frontend/revise_bad_unused_maybe.cat" "$FIXTURE_DIR/revise_bad_unused_maybe.cat" 'Unused Maybe['
run_check_pass "test_frontend/revise_ignore_must_use.cat" "$FIXTURE_DIR/revise_ignore_must_use.cat"

run_check_pass "test_frontend/revise_maybe.cat" "$FIXTURE_DIR/revise_maybe.cat"
echo "[air/pass] test_frontend/revise_maybe.cat"
revise_maybe_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_maybe.cat")"
if [[ "$revise_maybe_air_output" != *"add_one("* ]] ||
   [[ "$revise_maybe_air_output" != *"greet("* ]]; then
  echo "revised Maybe AIR did not reflect the Maybe[T] surface" >&2
  exit 1
fi

run_check_pass "test_frontend/revise_anchor.cat" "$FIXTURE_DIR/revise_anchor.cat"
run_check_fail "test_frontend/revise_bad_anchor_view_payload.cat" "$FIXTURE_DIR/revise_bad_anchor_view_payload.cat" 'Anchor.new(...) requires an owned payload'
run_check_fail "test_frontend/revise_bad_anchor_scoped_aggregate.cat" "$FIXTURE_DIR/revise_bad_anchor_scoped_aggregate.cat" 'Anchor.new(...) requires an owned payload'
run_check_fail "test_frontend/revise_bad_anchor_return_local.cat" "$FIXTURE_DIR/revise_bad_anchor_return_local.cat" "Returned ref value must come from one of the function's ref parameters."
run_check_pass "test_frontend/revise_anchor_choice.cat" "$FIXTURE_DIR/revise_anchor_choice.cat"

run_check_pass "test_frontend/revise_span_borrow.cat" "$FIXTURE_DIR/revise_span_borrow.cat"
run_check_fail "test_frontend/revise_bad_vec_index_borrow.cat" "$FIXTURE_DIR/revise_bad_vec_index_borrow.cat" 'Direct element borrows from Vec are forbidden.'
run_check_fail "test_frontend/revise_bad_vec_mutate_while_span.cat" "$FIXTURE_DIR/revise_bad_vec_mutate_while_span.cat" 'Cannot create ref mut view of `data` while `data` is still borrowed.'

run_check_pass "test_frontend/revise_field_borrow_paths.cat" "$FIXTURE_DIR/revise_field_borrow_paths.cat"
run_check_fail "test_frontend/revise_bad_field_prefix_borrow.cat" "$FIXTURE_DIR/revise_bad_field_prefix_borrow.cat" 'Cannot create ref mut view of `local.db.port` while `local.db` is still borrowed.'
run_check_fail "test_frontend/revise_bad_field_prefix_mutation.cat" "$FIXTURE_DIR/revise_bad_field_prefix_mutation.cat" 'Cannot create ref mut view of `local.db` while `local.db.host` is still borrowed.'
run_check_pass "test_frontend/revise_param_borrow_paths.cat" "$FIXTURE_DIR/revise_param_borrow_paths.cat"
run_check_fail "test_frontend/revise_bad_param_prefix_borrow.cat" "$FIXTURE_DIR/revise_bad_param_prefix_borrow.cat" 'Cannot create ref mut view of `cfg.db.port` while `cfg.db` is still borrowed.'
run_check_fail "test_frontend/revise_bad_param_prefix_mutation.cat" "$FIXTURE_DIR/revise_bad_param_prefix_mutation.cat" 'Cannot create ref mut view of `cfg.db` while `cfg.db.host` is still borrowed.'

run_check_fail "test_frontend/revise_bad_shape_ref_field.cat" "$FIXTURE_DIR/revise_bad_shape_ref_field.cat" "cannot store borrowed field 'stored'"
run_check_fail "test_frontend/revise_bad_shape_nested_ref_field.cat" "$FIXTURE_DIR/revise_bad_shape_nested_ref_field.cat" "cannot store borrowed field 'next'"
run_check_fail "test_frontend/revise_bad_shape_scoped_aggregate_field.cat" "$FIXTURE_DIR/revise_bad_shape_scoped_aggregate_field.cat" "cannot store borrowed field 'value'"
run_check_fail "test_frontend/revise_bad_choice_ref_payload.cat" "$FIXTURE_DIR/revise_bad_choice_ref_payload.cat" "Choice 'MaybeRef' cannot store"
run_check_fail "test_frontend/revise_bad_return_local_ref.cat" "$FIXTURE_DIR/revise_bad_return_local_ref.cat" "Returned ref value must come from one of the function's ref parameters."

run_check_pass "test_frontend/revise_scope_refs.cat" "$FIXTURE_DIR/revise_scope_refs.cat"
echo "[air/pass] test_frontend/revise_scope_refs.cat"
revise_scope_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_scope_refs.cat")"
if [[ "$revise_scope_air_output" != *"scope s"* ]] ||
   [[ "$revise_scope_air_output" != *"val a: ref[s] Str = ref[s] first"* ]]; then
  echo "revised scoped-ref AIR did not reflect the expected scope/ref[s] surface" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_scope_return.cat" "$FIXTURE_DIR/revise_bad_scope_return.cat" 'Scoped ref `ref[s] Str` cannot leave `scope s`.'
run_check_fail "test_frontend/revise_bad_scope_escape_assign.cat" "$FIXTURE_DIR/revise_bad_scope_escape_assign.cat" 'Scoped ref `ref[s] Str` cannot escape into `saved`.'
run_check_fail "test_frontend/revise_bad_scope_source_lifetime.cat" "$FIXTURE_DIR/revise_bad_scope_source_lifetime.cat" 'Source value for `ref[s] Str` does not live long enough for scope `s`.'

run_check_pass "test_frontend/revise_view_shape_scope.cat" "$FIXTURE_DIR/revise_view_shape_scope.cat"
echo "[air/pass] test_frontend/revise_view_shape_scope.cat"
revise_view_shape_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_view_shape_scope.cat")"
if [[ "$revise_view_shape_air_output" != *"air.view_shape Parser[s]"* ]] ||
   [[ "$revise_view_shape_air_output" != *"scope s"* ]] ||
   [[ "$revise_view_shape_air_output" != *"Parser {input: ref[s] source"* ]]; then
  echo "revised view-shape AIR did not reflect the expected scoped aggregate surface" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_view_shape_scope_field.cat" "$FIXTURE_DIR/revise_bad_view_shape_scope_field.cat" "must use the declared scope 's'"
run_check_fail "test_frontend/revise_bad_view_shape_escape.cat" "$FIXTURE_DIR/revise_bad_view_shape_escape.cat" 'Cannot return value `Parser[s]` because it is bound to scope `s`.'

run_check_pass "test_frontend/revise_scoped_type_propagation.cat" "$FIXTURE_DIR/revise_scoped_type_propagation.cat"
run_check_pass "test_frontend/revise_scoped_workspace" "$FIXTURE_DIR/revise_scoped_workspace"
run_check_fail "test_frontend/revise_bad_scoped_signature_mismatch.cat" "$FIXTURE_DIR/revise_bad_scoped_signature_mismatch.cat" "scope parameter 's' is bound to different active scopes"
run_check_fail "test_frontend/revise_bad_scoped_aggregate_mismatch.cat" "$FIXTURE_DIR/revise_bad_scoped_aggregate_mismatch.cat" "scope parameter 'shared' is bound to different active scopes"
run_check_fail "test_frontend/revise_bad_scoped_signature_unbound_argument.cat" "$FIXTURE_DIR/revise_bad_scoped_signature_unbound_argument.cat" "must carry a scope-bound value"
run_check_fail "test_frontend/revise_bad_scoped_signature_escape.cat" "$FIXTURE_DIR/revise_bad_scoped_signature_escape.cat" "Return type mismatch in function 'wrong_scope'"
run_check_fail "test_frontend/revise_bad_scoped_signature_short_lifetime.cat" "$FIXTURE_DIR/revise_bad_scoped_signature_short_lifetime.cat" 'does not live long enough for scope `s`'
run_check_fail "test_frontend/revise_bad_scoped_signature_owned_parameter.cat" "$FIXTURE_DIR/revise_bad_scoped_signature_owned_parameter.cat" "is not tied to scope parameter 's'"
run_check_fail "test_frontend/revise_bad_scoped_signature_unbound_return.cat" "$FIXTURE_DIR/revise_bad_scoped_signature_unbound_return.cat" "return scope parameter 's' is not bound by any function parameter"
run_check_pass "test_frontend/revise_nested_scoped_generic.cat" "$FIXTURE_DIR/revise_nested_scoped_generic.cat"
run_check_pass "test_frontend/revise_nested_scoped_workspace" "$FIXTURE_DIR/revise_nested_scoped_workspace"
run_check_fail "test_frontend/revise_bad_nested_scoped_generic_mismatch.cat" "$FIXTURE_DIR/revise_bad_nested_scoped_generic_mismatch.cat" "scope parameter 'borrow' is bound to different active scopes"
run_check_fail "test_frontend/revise_bad_nested_scoped_generic_field_mismatch.cat" "$FIXTURE_DIR/revise_bad_nested_scoped_generic_field_mismatch.cat" "Shape literal field type mismatch for 'value': expected Name[outer], got Name[inner]"
run_check_fail "test_frontend/revise_bad_nested_scoped_generic_escape.cat" "$FIXTURE_DIR/revise_bad_nested_scoped_generic_escape.cat" "Return type mismatch in function 'leak': expected Box[Name[borrow]], got Box[Name[inner]]"
run_check_fail "test_frontend/revise_bad_nested_scoped_generic_no_expected.cat" "$FIXTURE_DIR/revise_bad_nested_scoped_generic_no_expected.cat" "requires a concrete matching expected type"
run_check_fail "test_frontend/revise_bad_implements_mut_on_val.cat" "$FIXTURE_DIR/revise_bad_implements_mut_on_val.cat" "Method receiver type mismatch: expected ref mut Buffer, got Buffer"
run_check_pass "test_native/revise_static.cat" "$SCRIPT_DIR/../native/fixtures/revise_static.cat"
run_check_fail "test_frontend/revise_bad_static.cat" "$FIXTURE_DIR/revise_bad_static.cat" "error[parse]"
run_check_pass "test_native/revise_contract.cat" "$SCRIPT_DIR/../native/fixtures/revise_contract.cat"
run_check_pass "test_native/revise_arena.cat" "$SCRIPT_DIR/../native/fixtures/revise_arena.cat"
run_check_pass "test_native/revise_operator_overload.cat" "$SCRIPT_DIR/../native/fixtures/revise_operator_overload.cat"
run_check_fail "test_frontend/revise_bad_contract.cat" "$FIXTURE_DIR/revise_bad_contract.cat" "receiver type does not match contract"


TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

cat > "$TEMP_DIR/legacy_hold.cat" <<'EOF'
fn main() {
    hold value = 1
}
EOF
run_check_fail "legacy hold" "$TEMP_DIR/legacy_hold.cat" "Legacy binding syntax 'hold' has been removed."

cat > "$TEMP_DIR/legacy_slot.cat" <<'EOF'
fn main() {
    slot value = 1
}
EOF
run_check_fail "legacy slot" "$TEMP_DIR/legacy_slot.cat" "Legacy binding syntax 'slot' has been removed."

cat > "$TEMP_DIR/legacy_give.cat" <<'EOF'
fn main() -> Int32 {
    give 0
}
EOF
run_check_fail "legacy give" "$TEMP_DIR/legacy_give.cat" "Legacy return syntax 'give' has been removed."

cat > "$TEMP_DIR/legacy_look.cat" <<'EOF'
fn show(text: look Str) {
    println(text)
}

fn main() {}
EOF
run_check_fail "legacy look" "$TEMP_DIR/legacy_look.cat" "Legacy borrow syntax 'look' has been removed."

cat > "$TEMP_DIR/legacy_edit.cat" <<'EOF'
fn clear(text: edit Str) {
    println(text)
}

fn main() {}
EOF
run_check_fail "legacy edit" "$TEMP_DIR/legacy_edit.cat" "Legacy borrow syntax 'edit' has been removed."

cat > "$TEMP_DIR/legacy_when.cat" <<'EOF'
fn main() {
    when true {
    }
}
EOF
run_check_fail "legacy when" "$TEMP_DIR/legacy_when.cat" "Legacy conditional syntax 'when' has been removed."

cat > "$TEMP_DIR/legacy_otherwise.cat" <<'EOF'
fn main() {
    if true {
    } otherwise {
    }
}
EOF
run_check_fail "legacy otherwise" "$TEMP_DIR/legacy_otherwise.cat" "Legacy branch syntax 'otherwise' has been removed."

cat > "$TEMP_DIR/legacy_realm.cat" <<'EOF'
realm old.main

fn main() {}
EOF
run_check_fail "legacy realm" "$TEMP_DIR/legacy_realm.cat" "Legacy module syntax 'realm' has been removed."

cat > "$TEMP_DIR/legacy_of.cat" <<'EOF'
choice Box of T {
    Some(value: T)
}

fn main() {}
EOF
run_check_fail "legacy of" "$TEMP_DIR/legacy_of.cat" "Legacy generic syntax 'of' has been removed."
