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

run_check_warning() {
  local label="$1"
  local path="$2"
  local expect="$3"
  echo "[check/warning] $label"
  local output
  if ! output="$("$CLAW_EXE" check "$path" 2>&1)"; then
    echo "$label unexpectedly failed:\n$output" >&2
    exit 1
  fi
  if [[ "$output" != *"warning[semantic]"* || "$output" != *"$expect"* ]]; then
    echo "$label did not report the expected nonfatal warning: $expect" >&2
    echo "$output" >&2
    exit 1
  fi
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

run_air_fail() {
  local label="$1"
  local path="$2"
  local expect="$3"
  echo "[air/fail] $label"
  local output
  if output="$("$CLAW_EXE" air "$path" 2>&1)"; then
    echo "expected AIR generation for $label to fail, but it passed" >&2
    exit 1
  fi
  if [[ "$output" != *"$expect"* ]]; then
    echo "$label AIR did not report expected diagnostic substring: $expect" >&2
    echo "$output" >&2
    exit 1
  fi
}

echo "[check/pass] test_frontend/revise_surface.cat"
"$CLAW_EXE" check "$FIXTURE_DIR/revise_surface.cat"
run_check_warning "implicit block-tail return" "$SCRIPT_DIR/../native/fixtures/revise_implicit_return.cat" "only the final expression in a block returns implicitly"
run_check_fail "middle expression is not an implicit return" "$FIXTURE_DIR/revise_bad_implicit_return.cat" "must end with a value of type Int32 or use explicit return."
run_check_pass "Int32 ranges, membership, Float64 literals, and tuple projections" "$SCRIPT_DIR/../native/fixtures/revise_int32_range.cat"
run_check_pass "Range[Int32] scans with optional USize index" "$SCRIPT_DIR/../native/fixtures/revise_int32_range_scan.cat"
run_check_pass "Range[Char] construction, membership, scans, and Unicode scalar boundaries" "$SCRIPT_DIR/../native/fixtures/revise_char_range.cat"
run_check_pass "Range[UInt8] construction, membership, scans, USize indexes, and inclusive maximum" "$SCRIPT_DIR/../native/fixtures/revise_uint8_range.cat"
run_check_pass "Range[Char] type annotations" "$FIXTURE_DIR/revise_bad_range_char_type.cat"
run_check_pass "ordinary local shadowing across nested branches, tuples, and scans" "$SCRIPT_DIR/../native/fixtures/revise_local_shadowing_binding_identity.cat"
run_check_pass "builtin member receiver shadowing preserves binding identity" "$SCRIPT_DIR/../native/fixtures/revise_builtin_member_receiver_shadowing.cat"
run_check_pass "try result shadowing preserves binding identity" "$SCRIPT_DIR/../native/fixtures/revise_try_result_shadowing.cat"
run_check_pass "choice payload shadowing preserves binding identity" "$SCRIPT_DIR/../native/fixtures/revise_pick_payload_shadowing.cat"
run_check_pass "Anchor choice payload shadowing preserves cleanup identity" "$SCRIPT_DIR/../native/fixtures/revise_pick_anchor_payload_shadowing.cat"
run_check_fail "same-scope duplicate local names remain rejected" "$FIXTURE_DIR/revise_bad_same_scope_duplicate_binding.cat" "Duplicate variable declaration: repeated"
run_check_fail "indexed scans reject Str" "$FIXTURE_DIR/revise_bad_indexed_scan_str.cat" "An indexed scan is currently supported only over Range[Int32], Range[UInt8], or Range[Char]."
range_scan_air_output="$("$CLAW_EXE" air "$SCRIPT_DIR/../native/fixtures/revise_int32_range_scan.cat")"
if [[ "$range_scan_air_output" != *"scan item: Int32 over (1 .. 4) : Range[Int32]"* ]] ||
   [[ "$range_scan_air_output" != *"scan item: Int32, index: USize over (3 ..= 5) : Range[Int32]"* ]]; then
  echo "Range scan AIR did not preserve optional index binding or item/index types" >&2
  echo "$range_scan_air_output" >&2
  exit 1
fi
run_check_fail "other integer range widths remain unsupported" "$FIXTURE_DIR/revise_bad_int32_range_int64.cat" "Range bounds currently support only Int32"
run_check_fail "floating-point range endpoints remain unsupported" "$FIXTURE_DIR/revise_bad_int32_range_float.cat" "Range bounds currently support only Int32"
run_check_fail "Range endpoints reject mixed scalar types" "$FIXTURE_DIR/revise_bad_range_mixed_bounds.cat" "Range bounds currently support only Int32, UInt8, or Char with matching types"
run_check_fail "Range membership rejects mixed scalar types" "$FIXTURE_DIR/revise_bad_range_mixed_membership.cat" 'Operator `in` requires a matching Char/Range[Char], Int32/Range[Int32], or UInt8/Range[UInt8] pair'
run_check_fail "Str slicing remains restricted to Range[Int32]" "$FIXTURE_DIR/revise_bad_str_char_range_index.cat" "Index expression must have type USize, got Range[Char]"
run_check_fail "Str slicing rejects Range[UInt8]" "$FIXTURE_DIR/revise_bad_str_uint8_range_index.cat" "Index expression must have type USize, got Range[UInt8]"
run_check_fail "UInt8 and Int32 range bounds cannot be mixed" "$FIXTURE_DIR/revise_bad_range_mixed_uint8_int32.cat" "Range bounds currently support only Int32, UInt8, or Char with matching types"
run_check_fail "UInt8 membership requires Range[UInt8]" "$FIXTURE_DIR/revise_bad_range_mixed_uint8_membership.cat" 'Operator `in` requires a matching Char/Range[Char], Int32/Range[Int32], or UInt8/Range[UInt8] pair'
run_check_fail "other Range element types remain unsupported" "$FIXTURE_DIR/revise_bad_range_int64.cat" "Only Range[Int32], Range[UInt8], and Range[Char] are supported"
run_check_fail "UInt16 Range remains unsupported" "$FIXTURE_DIR/revise_bad_range_uint16.cat" "Only Range[Int32], Range[UInt8], and Range[Char] are supported"
run_check_fail "membership requires a Range right operand" "$FIXTURE_DIR/revise_bad_range_membership_rhs.cat" 'Operator `in` requires a matching Char/Range[Char], Int32/Range[Int32], or UInt8/Range[UInt8] pair'
run_check_fail "membership rejects a non-scalar left operand" "$FIXTURE_DIR/revise_bad_range_membership_left.cat" 'Operator `in` requires a matching Char/Range[Char], Int32/Range[Int32], or UInt8/Range[UInt8] pair'
run_check_fail "range operators are non-associative" "$FIXTURE_DIR/revise_bad_int32_range_chain.cat" 'Range operators `..` and `..=` are non-associative'
run_check_fail "range comparisons are not implicit membership" "$FIXTURE_DIR/revise_bad_range_comparison.cat" 'Range values support `in` membership only'
run_check_fail "trailing decimal dot remains a single-dot projection" "$FIXTURE_DIR/revise_bad_trailing_decimal_dot.cat" "Type 'IntLiteral' does not expose fields."
run_check_pass "plain tuple literals, types, projections, and branch values" "$SCRIPT_DIR/../native/fixtures/revise_tuples.cat"
run_check_fail "nested tuple projection assignment remains unsupported" "$FIXTURE_DIR/revise_bad_nested_tuple_assignment.cat" "Member assignment requires mutable storage or a ref mut view."
run_check_fail "tuple projection index out of range" "$FIXTURE_DIR/revise_bad_tuple_projection.cat" "Tuple projection index 2 is out of range"
run_check_fail "tuple fields without a safe destructor remain rejected" "$FIXTURE_DIR/revise_bad_tuple_owned_field.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_check_fail "tuple literal does not match expected arity" "$FIXTURE_DIR/revise_bad_tuple_arity.cat" "Tuple literal has 1 element(s), but the expected tuple type has 2."
run_check_pass "plain tuple destructuring with wildcard and singleton patterns" "$SCRIPT_DIR/../native/fixtures/revise_tuple_destructuring.cat"
run_check_fail "nested tuple destructuring remains unsupported" "$FIXTURE_DIR/revise_bad_tuple_destructuring.cat" "Nested tuple patterns in bindings are not yet supported."
run_check_fail "tuple destructuring pattern arity mismatch" "$FIXTURE_DIR/revise_bad_tuple_destructuring_arity.cat" "Tuple destructuring expects 2 binding(s), but the tuple has 3 field(s)."
run_check_fail "tuple destructuring requires a tuple value" "$FIXTURE_DIR/revise_bad_tuple_destructuring_non_tuple.cat" "Tuple destructuring requires a tuple value"
run_check_fail "tuple destructuring consumes the source tuple" "$FIXTURE_DIR/revise_bad_tuple_destructuring_reuse.cat" "Use of moved value -> pair"
run_check_fail "tuple destructuring of a view tuple remains unsupported" "$FIXTURE_DIR/revise_bad_tuple_destructuring_view.cat" "cannot destructure a view tuple"
run_check_fail "tuple view fields remain unsupported" "$FIXTURE_DIR/revise_bad_tuple_view_field.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_check_fail "generic tuple fields remain unsupported" "$FIXTURE_DIR/revise_bad_tuple_destructuring_generic.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_check_fail "Span borrow-carrier tuple fields remain unsupported" "$FIXTURE_DIR/revise_bad_tuple_destructuring_span.cat" "Tuple destructuring currently supports plain fields and Anchor[T] fields with plain T; element 0 has type Span[Int32]."
run_check_fail "mutable tuple destructuring remains unsupported" "$FIXTURE_DIR/revise_bad_tuple_destructuring_mutable.cat" 'Tuple destructuring is currently supported only for `val` bindings.'
run_check_pass "owned Anchor tuple destructuring and recursive cleanup" "$SCRIPT_DIR/../native/fixtures/revise_tuple_owned_destructuring.cat"
run_check_fail "tuple Anchor payload without a destructor remains unsupported" "$FIXTURE_DIR/revise_bad_tuple_anchor_owned_payload.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_check_fail "owned tuple alias cannot reuse moved source" "$FIXTURE_DIR/revise_bad_tuple_owned_alias.cat" "Use of moved value -> pair"
run_check_fail "owned tuple field cannot be moved through projection" "$FIXTURE_DIR/revise_bad_tuple_owned_projection_move.cat" "Cannot move an owned tuple field through numeric projection"
run_check_fail "owned tuple field cannot be replaced directly" "$FIXTURE_DIR/revise_bad_tuple_owned_field_replace.cat" "Replacing an owned tuple field directly is unsupported"
run_check_fail "nested owned tuple projection cannot be partially destructured" "$FIXTURE_DIR/revise_bad_tuple_owned_nested_projection.cat" "Cannot destructure an owned tuple projection"
run_check_fail "owned-field tuple cannot be embedded in a shape" "$FIXTURE_DIR/revise_bad_tuple_owned_shape_field.cat" "Shape fields cannot contain tuples with owned fields"
run_check_fail "owned-field tuple cannot be embedded in a generic container" "$FIXTURE_DIR/revise_bad_tuple_owned_container.cat" "Tuples with owned fields are supported only as direct tuple values"
run_check_pass "recursive tuple patterns in pick statements and expressions" "$SCRIPT_DIR/../native/fixtures/revise_tuple_pick.cat"
run_check_fail "tuple pattern requires tuple selector in statement pick" "$FIXTURE_DIR/revise_bad_tuple_pick_pattern.cat" "Tuple pick pattern at value requires a tuple value"
run_check_fail "tuple pattern requires tuple selector in pick expression" "$FIXTURE_DIR/revise_bad_tuple_pick_expression_pattern.cat" "Tuple pick pattern at value requires a tuple value"
run_check_fail "nested tuple pick pattern type mismatch" "$FIXTURE_DIR/revise_bad_tuple_pick_nested_type.cat" "Tuple pick pattern at value.0 requires a tuple value"
run_check_fail "nested tuple pick pattern arity mismatch" "$FIXTURE_DIR/revise_bad_tuple_pick_arity.cat" "Tuple pick pattern at value.0 expects 1 field(s), but the tuple has 2."
run_check_fail "tuple pick wildcard must be last" "$FIXTURE_DIR/revise_bad_tuple_pick_wildcard_nonfinal.cat" "The '_' catch-all branch must be the last branch in a pick."
run_check_fail "tuple pick branch after wildcard is unreachable" "$FIXTURE_DIR/revise_bad_tuple_pick_wildcard_nonfinal.cat" "Unreachable pick branch after the '_' catch-all branch."
run_check_fail "tuple pick consumes the source tuple" "$FIXTURE_DIR/revise_bad_tuple_pick_reuse.cat" "Use of moved value -> pair"
run_check_fail "branch after irrefutable tuple pick pattern is unreachable" "$FIXTURE_DIR/revise_bad_tuple_pick_after_irrefutable.cat" "Unreachable pick branch after an irrefutable tuple pattern."
run_check_fail "tuple pick rejects view tuple fields" "$FIXTURE_DIR/revise_bad_tuple_pick_view.cat" "Tuple pick patterns currently support only plain, concrete, non-view fields"
run_check_fail "tuple pick rejects owned tuple fields" "$FIXTURE_DIR/revise_bad_tuple_pick_owned.cat" "Tuple pick patterns currently support only plain, concrete, non-view fields"
run_check_fail "tuple pick rejects Span borrow-carrier fields" "$FIXTURE_DIR/revise_bad_tuple_pick_span.cat" "Tuple pick patterns currently support only plain, concrete, non-view fields"
run_check_fail "owned tuple pick patterns remain unsupported" "$FIXTURE_DIR/revise_bad_tuple_owned_pick.cat" "Tuple pick patterns currently support only plain, concrete, non-view fields"
run_check_fail "pick cannot leak a temporary owned tuple through wildcard" "$FIXTURE_DIR/revise_bad_tuple_owned_pick_temporary.cat" "Pick over a temporary owned tuple is unsupported"
run_check_fail "owned temporary without destructor is rejected" "$FIXTURE_DIR/revise_bad_discarded_owned_temporary.cat" 'Cannot discard owned temporary of type `Str` because no safe destructor is available.'
run_check_fail "nested Anchor payload requires recursive destruction" "$FIXTURE_DIR/revise_bad_anchor_nested_payload.cat" 'Anchor.new(...) cannot store payload type `Anchor[Int32]` because it contains a nested Anchor'
run_check_fail "nested Anchor payload type is unsupported" "$FIXTURE_DIR/revise_bad_anchor_nested_type.cat" "is unsupported because an Anchor payload cannot contain another Anchor"
run_check_fail "generic Shape specialization cannot nest Anchor payloads" "$FIXTURE_DIR/revise_bad_anchor_nested_shape_specialization.cat" 'is unsupported because an Anchor payload cannot contain another Anchor'
run_check_fail "Anchor.new cannot wrap a Shape with an Anchor field" "$FIXTURE_DIR/revise_bad_anchor_nested_shape_payload.cat" 'is unsupported because an Anchor payload cannot contain another Anchor'
run_check_pass "valid AnchorBox[Str] and Anchor[Int32] specializations" "$SCRIPT_DIR/../native/fixtures/revise_anchor_box_specialization.cat"
run_check_warning "overloaded Add returning discarded Anchor" "$SCRIPT_DIR/../native/fixtures/revise_discarded_binary_anchor_temporary.cat" "only the final expression in a block returns implicitly"
run_check_fail "discarded ShapeInit without destructor is rejected" "$FIXTURE_DIR/revise_bad_discarded_shape_init.cat" 'Cannot discard owned temporary of type `Record` because no safe destructor is available.'
run_check_fail "member projection from owned temporary is rejected" "$FIXTURE_DIR/revise_bad_discarded_member_temporary.cat" "Cannot discard an expression projected from an owned temporary"
run_check_fail "index projection from owned temporary is rejected" "$FIXTURE_DIR/revise_bad_discarded_index_temporary.cat" "Cannot discard an expression projected from an owned temporary"
run_check_fail "borrowing method on owned temporary is rejected" "$FIXTURE_DIR/revise_bad_discarded_borrowed_receiver.cat" "Cannot discard a call on an owned temporary receiver"

echo "[air/pass] recursive tuple pick patterns"
tuple_pick_air_output="$("$CLAW_EXE" air "$SCRIPT_DIR/../native/fixtures/revise_tuple_pick.cat")"
if [[ "$tuple_pick_air_output" != *"((left, _), right)"* ]] ||
   [[ "$tuple_pick_air_output" != *"((_, middle), right)"* ]] ||
   [[ "$tuple_pick_air_output" != *"(only,)"* ]]; then
  echo "tuple-pick AIR did not preserve recursive, wildcard, and singleton patterns" >&2
  echo "$tuple_pick_air_output" >&2
  exit 1
fi

echo "[air/golden] tuple expression values and nested arity"
tuple_expr_air_output="$("$CLAW_EXE" air "$SCRIPT_DIR/../native/fixtures/revise_tuples.cat")"
if ! diff -u "$FIXTURE_DIR/revise_tuples.air.golden" <(printf '%s\n' "$tuple_expr_air_output"); then
  echo "tuple-expression AIR output differs from the golden" >&2
  exit 1
fi
if [[ "$tuple_expr_air_output" == *"<expr>"* ]] ||
   [[ "$tuple_expr_air_output" != *"return (40, 2) : Tuple[Int64, Int64]"* ]] ||
   [[ "$tuple_expr_air_output" != *"return ((1, 2), 40) : Tuple[Tuple[Int32, Int32], Int32]"* ]] ||
   [[ "$tuple_expr_air_output" != *"val singleton: Tuple[Int32] = (42,) : Tuple[Int32]"* ]] ||
   [[ "$tuple_expr_air_output" != *"expr (20, 22) : Tuple[Int64, Int64]"* ]] ||
   [[ "$tuple_expr_air_output" != *"expr (10, 32) : Tuple[Int64, Int64]"* ]] ||
   [[ "$tuple_expr_air_output" != *"val grouped: Int32 = (20 + 22) : Int32"* ]]; then
  echo "tuple-expression AIR did not show tuple values/arity or changed grouping output" >&2
  echo "$tuple_expr_air_output" >&2
  exit 1
fi

echo "[air/pass] tuple destructuring pattern names"
tuple_destructuring_air_output="$("$CLAW_EXE" air "$SCRIPT_DIR/../native/fixtures/revise_tuple_destructuring.cat")"
if [[ "$tuple_destructuring_air_output" != *"val (left, right) = pair : Tuple[Int32, Int32]"* ]] ||
   [[ "$tuple_destructuring_air_output" != *"val (_, kept) ="* ]]; then
  echo "tuple destructuring AIR did not preserve named and wildcard pattern bindings" >&2
  echo "$tuple_destructuring_air_output" >&2
  exit 1
fi

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
run_check_pass "test_frontend/revise_module_aliases" "$FIXTURE_DIR/revise_module_aliases"
run_check_fail "test_frontend/revise_bad_duplicate_module_aliases" "$FIXTURE_DIR/revise_bad_duplicate_module_aliases" "Duplicate imported name: math"
run_check_pass "test_frontend/revise_super_import" "$FIXTURE_DIR/revise_super_import"
run_check_fail "test_frontend/revise_bad_super_directory" "$FIXTURE_DIR/revise_bad_super_directory" "Unable to resolve sibling module 'ghost' via super import."
run_check_fail "test_frontend/revise_bad_case_module_path" "$FIXTURE_DIR/revise_bad_case_module_path" "Module name segment 'UpperMath' must start with a lowercase letter."
run_check_fail "test_frontend/revise_bad_case_module_alias" "$FIXTURE_DIR/revise_bad_case_module_alias" "Module name 'MathOps' must start with a lowercase letter."
run_check_fail "test_frontend/revise_bad_pub.cat" "$FIXTURE_DIR/revise_bad_pub.cat" "Unexpected token at top level."
run_check_fail "test_frontend/revise_bad_module_cycle" "$FIXTURE_DIR/revise_bad_module_cycle" "Cyclic module dependency detected."
run_check_fail "test_frontend/revise_bad_private_type_export" "$FIXTURE_DIR/revise_bad_private_type_export" "Unknown type: PrivatePayload"
run_check_fail "test_frontend/revise_bad_private_item_import" "$FIXTURE_DIR/revise_bad_private_item_import" "does not share item 'hidden'"
run_validate_fail "test_frontend/revise_bad_main_return" "$FIXTURE_DIR/revise_bad_main_return" '`main` must take no parameters and return Unit or Int32.'
run_check_fail "test_frontend/revise_bad_double_main.cat" "$FIXTURE_DIR/revise_bad_double_main.cat" 'main.cat must contain exactly one `fn main` declaration.'
run_validate_fail "test_frontend/revise_bad_non_entry_main" "$FIXTURE_DIR/revise_bad_non_entry_main" '`fn main` is only allowed in root main.cat.'
run_check_fail "test_frontend/revise_bad_call_main.cat" "$FIXTURE_DIR/revise_bad_call_main.cat" '`main` is the program entry point and cannot be called like a normal function.'

run_check_pass "test_frontend/revise_builtin_methods.cat" "$FIXTURE_DIR/revise_builtin_methods.cat"
run_check_pass "test_frontend/revise_builtin_method_views.cat" "$FIXTURE_DIR/revise_builtin_method_views.cat"
run_check_fail "test_frontend/revise_bad_builtin_method_receiver.cat" "$FIXTURE_DIR/revise_bad_builtin_method_receiver.cat" 'Method receiver type mismatch: expected ref mut Vec[Int32], got Vec[Int32]'
run_check_fail "Vec.truncate is rejected before LLVM lowering" "$FIXTURE_DIR/revise_bad_vec_truncate_unsupported.cat" "Type 'Vec' does not provide method 'truncate'."
run_check_fail "Queue.truncate is rejected before LLVM lowering" "$FIXTURE_DIR/revise_bad_queue_truncate_unsupported.cat" "Type 'Queue' does not provide method 'truncate'."
run_check_pass "test_frontend/revise_str_search_methods.cat" "$FIXTURE_DIR/revise_str_search_methods.cat"
run_check_pass "test_frontend/revise_choice_resolution.cat" "$FIXTURE_DIR/revise_choice_resolution.cat"
echo "[air/golden] test_frontend/revise_choice_resolution.cat"
choice_resolution_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_choice_resolution.cat")"
if ! diff -u "$FIXTURE_DIR/revise_choice_resolution.air.golden" <(printf '%s\n' "$choice_resolution_air_output"); then
  echo "Choice-resolution AIR output differs from the golden" >&2
  exit 1
fi
run_check_pass "test_native/revise_if_pick_values.cat" "$SCRIPT_DIR/../native/fixtures/revise_if_pick_values.cat"
echo "[air/pass] test_native/revise_if_pick_values.cat"
if ! value_air_output="$("$CLAW_EXE" air "$SCRIPT_DIR/../native/fixtures/revise_if_pick_values.cat")" ||
   [[ "$value_air_output" != *"return if flag : Bool {"* ]] ||
   [[ "$value_air_output" != *"pick route : Route {"* ]] ||
   [[ "$value_air_output" != *"if false : Bool"* ]]; then
  echo "if/pick value AIR did not show the expected branch expressions and inferred types" >&2
  exit 1
fi
run_check_fail "if value without else" "$FIXTURE_DIR/revise_bad_if_value_missing_else.cat" "'if' as a value requires an 'else' branch."
run_check_fail "incompatible if branch types" "$FIXTURE_DIR/revise_bad_if_value_incompatible.cat" "Incompatible branch types in if expression"
run_check_fail "incompatible pick branch types" "$FIXTURE_DIR/revise_bad_pick_value_incompatible.cat" "Branch type mismatch in pick expression: expected Int32, got Str."
run_check_fail "non-exhaustive pick value" "$FIXTURE_DIR/revise_bad_pick_value_non_exhaustive.cat" "Non-exhaustive pick for choice type 'Route'."
run_check_fail "wildcard must be last in pick value" "$FIXTURE_DIR/revise_bad_pick_value_wildcard_nonfinal.cat" "The '_' catch-all branch must be the last branch in a pick."
run_check_fail "branch after wildcard is unreachable" "$FIXTURE_DIR/revise_bad_pick_value_wildcard_nonfinal.cat" "Unreachable pick branch after the '_' catch-all branch."
run_check_fail "pick value binding name casing" "$FIXTURE_DIR/revise_bad_pick_value_binding_case.cat" "Variable name 'Value' must start with a lowercase letter."
run_air_fail "if value without else" "$FIXTURE_DIR/revise_bad_if_value_missing_else.cat" "'if' as a value requires an 'else' branch."
run_check_fail "test_frontend/revise_bad_result_choice_resolution.cat" "$FIXTURE_DIR/revise_bad_result_choice_resolution.cat" '`try` requires Result to define Ok(T) and Fail(E) single-payload variants.'
run_check_fail "test_frontend/revise_bad_choice_variant_case.cat" "$FIXTURE_DIR/revise_bad_choice_variant_case.cat" "must start with a lowercase letter"
run_check_fail "test_frontend/revise_bad_choice_case_mismatch.cat" "$FIXTURE_DIR/revise_bad_choice_case_mismatch.cat" "Unknown choice variant 'Circle' for type 'Shape'."
run_check_pass "test_frontend/revise_name_case.cat" "$FIXTURE_DIR/revise_name_case.cat"
run_check_fail "test_frontend/revise_bad_case_type.cat" "$FIXTURE_DIR/revise_bad_case_type.cat" "Type name 'point' must start with an uppercase letter."
run_check_fail "test_frontend/revise_bad_case_function.cat" "$FIXTURE_DIR/revise_bad_case_function.cat" "Function name 'UpperFunction' must start with a lowercase letter."
run_check_fail "test_frontend/revise_bad_case_parameter.cat" "$FIXTURE_DIR/revise_bad_case_parameter.cat" "Parameter name 'UpperParameter' must start with a lowercase letter."
run_check_fail "test_frontend/revise_bad_case_variable.cat" "$FIXTURE_DIR/revise_bad_case_variable.cat" "Variable name 'UpperVariable' must start with a lowercase letter."
run_check_fail "test_frontend/revise_bad_case_field.cat" "$FIXTURE_DIR/revise_bad_case_field.cat" "Field name 'UpperField' must start with a lowercase letter."
run_check_fail "test_frontend/revise_bad_case_custom_variant.cat" "$FIXTURE_DIR/revise_bad_case_custom_variant.cat" "Choice variant 'Ok' must start with a lowercase letter"
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

run_check_pass "test_native/revise_try_contract.cat" "$SCRIPT_DIR/../native/fixtures/revise_try_contract.cat"
echo "[air/pass] test_native/revise_try_contract.cat"
try_contract_air_output="$("$CLAW_EXE" air "$SCRIPT_DIR/../native/fixtures/revise_try_contract.cat")"
if [[ "$try_contract_air_output" != *"val inner_result = try nested() : Result[Result[Int32, Str], Str]"* ]] ||
   [[ "$try_contract_air_output" != *"return inner_result : Result[Int32, Str]"* ]]; then
  echo "try AIR did not preserve exactly one Result layer" >&2
  exit 1
fi

run_check_pass "try error type through imported item alias" "$FIXTURE_DIR/revise_try_error_alias"
echo "[air/pass] try error type through imported item alias"
try_alias_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_try_error_alias")"
if [[ "$try_alias_air_output" != *"val value = try produce()"* ]]; then
  echo "try AIR rejected or lost the canonical identity of an imported generic error type" >&2
  exit 1
fi

run_check_fail "test_frontend/revise_bad_try_return.cat" "$FIXTURE_DIR/revise_bad_try_return.cat" '`try` shorthand requires the current function to return Result[T, E].'
run_check_fail "test_frontend/revise_bad_try_var.cat" "$FIXTURE_DIR/revise_bad_try_var.cat" '`try` is only allowed in a `val` initializer.'
run_check_fail "try in return expression" "$FIXTURE_DIR/revise_bad_try_return_expr.cat" 'Expected expression.'
run_check_fail "shorthand try with a different error type" "$FIXTURE_DIR/revise_bad_try_error_type.cat" '`try` shorthand error type mismatch: expected Int32, got Str'
run_check_fail "nonterminal try else block" "$FIXTURE_DIR/revise_bad_try_else_nonterminal.cat" '`try ... else` failure block must terminate the current control path.'
run_check_fail "owned Result reuse after try" "$FIXTURE_DIR/revise_bad_try_owned_reuse.cat" 'Use of moved value -> result'
run_check_fail "try error alias with mismatched generic argument" "$FIXTURE_DIR/revise_bad_try_alias_generic_mismatch" '`try` shorthand error type mismatch'
run_check_fail "same nominal spelling in different modules" "$FIXTURE_DIR/revise_bad_try_alias_cross_module" '`try` shorthand error type mismatch'
run_air_fail "var try initializer" "$FIXTURE_DIR/revise_bad_try_var.cat" '`try` is only allowed in a `val` initializer.'
run_air_fail "try in return expression" "$FIXTURE_DIR/revise_bad_try_return_expr.cat" 'Expected expression.'
run_air_fail "shorthand try with a different error type" "$FIXTURE_DIR/revise_bad_try_error_type.cat" '`try` shorthand error type mismatch: expected Int32, got Str'
run_air_fail "nonterminal try else block" "$FIXTURE_DIR/revise_bad_try_else_nonterminal.cat" '`try ... else` failure block must terminate the current control path.'
run_air_fail "try error alias with mismatched generic argument" "$FIXTURE_DIR/revise_bad_try_alias_generic_mismatch" '`try` shorthand error type mismatch'
run_air_fail "same nominal spelling in different modules" "$FIXTURE_DIR/revise_bad_try_alias_cross_module" '`try` shorthand error type mismatch'
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
operator_groups_fixture="$SCRIPT_DIR/../native/fixtures/revise_operator_groups_ab.cat"
run_check_pass "test_native/revise_operator_groups_ab.cat" "$operator_groups_fixture"
operator_groups_air="$("$CLAW_EXE" air "$operator_groups_fixture")"
if [[ "$operator_groups_air" != *"val arithmetic_precedence: Int32 = (2 + (3 * 4)) : Int32"* ]] ||
   [[ "$operator_groups_air" != *"val subtraction_left_assoc: Int32 = ((10 - 3) - 2) : Int32"* ]] ||
   [[ "$operator_groups_air" != *"val logical_precedence: Bool = (true or (false and observe(false : Bool))) : Bool"* ]] ||
   [[ "$operator_groups_air" != *"val negated: Bool = (not false) : Bool"* ]]; then
  echo "operator A-B AIR did not preserve the approved precedence and associativity" >&2
  exit 1
fi
if [[ "$operator_groups_air" != *"val comparisons: Bool = ((((((1 + (2 * 3)) == 7) and (2 <= 2)) and (3 > 2)) and (3 >= 3)) and (4 != 5)) : Bool"* ]]; then
  echo "comparison AIR did not place arithmetic before comparison and and" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_operator_comparison_chain.cat" "$FIXTURE_DIR/revise_bad_operator_comparison_chain.cat" "non-associative; combine comparisons with"
run_check_fail "test_frontend/revise_bad_operator_not_operand.cat" "$FIXTURE_DIR/revise_bad_operator_not_operand.cat" "Unary operator 'not' requires Bool"
run_check_fail "test_frontend/revise_bad_operator_logical_operand.cat" "$FIXTURE_DIR/revise_bad_operator_logical_operand.cat" "Logical operator 'and' requires Bool operands"
run_check_fail "test_frontend/revise_bad_operator_modulo_float.cat" "$FIXTURE_DIR/revise_bad_operator_modulo_float.cat" "Modulo operator '%' requires integer operands"
run_check_fail "test_frontend/revise_bad_operator_compound_uninitialized.cat" "$FIXTURE_DIR/revise_bad_operator_compound_uninitialized.cat" "Use of uninitialized value -> count"
run_check_fail "test_frontend/revise_bad_operator_compound_immutable.cat" "$FIXTURE_DIR/revise_bad_operator_compound_immutable.cat" "Cannot assign to immutable binding: count"
short_circuit_ownership_fixture="$SCRIPT_DIR/../native/fixtures/revise_operator_short_circuit_ownership.cat"
run_check_pass "test_native/revise_operator_short_circuit_ownership.cat" "$short_circuit_ownership_fixture"
short_circuit_ownership_oir="$("$CLAW_EXE" oir "$short_circuit_ownership_fixture")"
if [[ "$short_circuit_ownership_oir" != *"drop anchor : Anchor[Str]"* ]]; then
  echo "short-circuit ownership OIR did not retain the caller's Anchor drop" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_operator_conditional_move.cat" "$FIXTURE_DIR/revise_bad_operator_conditional_move.cat" "may conditionally move owned value"
float_unary_fixture="$SCRIPT_DIR/../native/fixtures/revise_float64_unary_negation.cat"
run_check_pass "test_native/revise_float64_unary_negation.cat" "$float_unary_fixture"
float_unary_oir="$("$CLAW_EXE" oir "$float_unary_fixture")"
if [[ "$float_unary_oir" != *"neg value : Float64"* ]]; then
  echo "Float64 unary OIR did not retain an explicit unary negation" >&2
  exit 1
fi
run_check_pass "test_native/revise_int32_min_literal.cat" "$SCRIPT_DIR/../native/fixtures/revise_int32_min_literal.cat"
run_check_fail "test_frontend/revise_bad_numeric_literal_positive_int32_boundary.cat" "$FIXTURE_DIR/revise_bad_numeric_literal_positive_int32_boundary.cat" "does not fit target type Int32"
field_uninitialized_fixture="$FIXTURE_DIR/revise_bad_operator_compound_field_uninitialized.cat"
echo "[check/fail] test_frontend/revise_bad_operator_compound_field_uninitialized.cat"
if field_uninitialized_output="$("$CLAW_EXE" check "$field_uninitialized_fixture" 2>&1)"; then
  echo "expected field compound assignment on uninitialized storage to fail" >&2
  exit 1
fi
field_uninitialized_count="$(grep -Fc "Use of uninitialized value -> meter" <<<"$field_uninitialized_output" || true)"
if [[ "$field_uninitialized_count" != "1" ]]; then
  echo "field compound assignment expected one uninitialized diagnostic, got $field_uninitialized_count" >&2
  echo "$field_uninitialized_output" >&2
  exit 1
fi
run_check_fail "test_frontend/revise_bad_contract.cat" "$FIXTURE_DIR/revise_bad_contract.cat" "receiver type does not match contract"


TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

cat > "$TEMP_DIR/legacy_hold.cat" <<'EOF'
fn main() {
    hold value = 1
}
EOF
run_check_fail "legacy hold" "$TEMP_DIR/legacy_hold.cat" "error["

cat > "$TEMP_DIR/legacy_slot.cat" <<'EOF'
fn main() {
    slot value = 1
}
EOF
run_check_fail "legacy slot" "$TEMP_DIR/legacy_slot.cat" "error["

cat > "$TEMP_DIR/legacy_give.cat" <<'EOF'
fn main() -> Int32 {
    give 0
}
EOF
run_check_fail "legacy give" "$TEMP_DIR/legacy_give.cat" "error["

cat > "$TEMP_DIR/legacy_look.cat" <<'EOF'
fn show(text: look Str) {
    println(text)
}

fn main() {}
EOF
run_check_fail "legacy look" "$TEMP_DIR/legacy_look.cat" "error["

cat > "$TEMP_DIR/legacy_edit.cat" <<'EOF'
fn clear(text: edit Str) {
    println(text)
}

fn main() {}
EOF
run_check_fail "legacy edit" "$TEMP_DIR/legacy_edit.cat" "error["

cat > "$TEMP_DIR/legacy_when.cat" <<'EOF'
fn main() {
    when true {
    }
}
EOF
run_check_fail "legacy when" "$TEMP_DIR/legacy_when.cat" "error["

cat > "$TEMP_DIR/legacy_otherwise.cat" <<'EOF'
fn main() {
    if true {
    } otherwise {
    }
}
EOF
run_check_fail "legacy otherwise" "$TEMP_DIR/legacy_otherwise.cat" "error["

cat > "$TEMP_DIR/legacy_realm.cat" <<'EOF'
realm old.main

fn main() {}
EOF
run_check_fail "legacy realm" "$TEMP_DIR/legacy_realm.cat" "error["

cat > "$TEMP_DIR/legacy_of.cat" <<'EOF'
choice Box of T {
    Some(value: T)
}

fn main() {}
EOF
run_check_fail "legacy of" "$TEMP_DIR/legacy_of.cat" "error["

cat > "$TEMP_DIR/legacy_lift.cat" <<'EOF'
choice Result[T, E] {
    Ok(value: T)
    Fail(cause: E)
}
fn load() -> Result[Int32, Str] {
    lift Ok(1) as value fail issue {
        return Fail(issue)
    }
    return Ok(value)
}
fn main() {}
EOF
run_check_fail "legacy lift" "$TEMP_DIR/legacy_lift.cat" "error["

cat > "$TEMP_DIR/legacy_link.cat" <<'EOF'
link core.io
fn main() {}
EOF
run_check_fail "legacy link import" "$TEMP_DIR/legacy_link.cat" "error["
for legacy_type in Text Rune Byte Bytes Bits8 Bits16 Bits32 Bits64 Bits128 ISize; do
  legacy_type_file="$TEMP_DIR/legacy_type_${legacy_type}.cat"
  cat > "$legacy_type_file" <<EOF
fn accepts(value: ${legacy_type}) {}
fn main() {}
EOF
  run_check_fail "legacy type ${legacy_type}" "$legacy_type_file" "Unknown type: ${legacy_type}"
done

run_check_pass "test_frontend/revise_operator_index_compound.cat" \
  "$FIXTURE_DIR/revise_operator_index_compound.cat"
run_check_fail "test_frontend/revise_bad_operator_index_compound_immutable.cat" \
  "$FIXTURE_DIR/revise_bad_operator_index_compound_immutable.cat" \
  "Index assignment requires mutable storage or a ref mut view."
run_check_fail "test_frontend/revise_bad_operator_str_index_literal.cat" \
  "$FIXTURE_DIR/revise_bad_operator_str_index_literal.cat" \
  "Indexed assignment is not supported for Str; string indexing is read-only."
run_check_fail "test_frontend/revise_bad_operator_str_index_owned.cat" \
  "$FIXTURE_DIR/revise_bad_operator_str_index_owned.cat" \
  "Indexed assignment is not supported for Str; string indexing is read-only."
run_check_pass "PRD §11 integer radix, separators, exact suffixes, Int128/UInt128, decimal and tight-range regressions" "$FIXTURE_DIR/revise_integer_radix_literals.cat"
run_check_fail "empty hexadecimal literal" "$FIXTURE_DIR/revise_bad_integer_empty_radix.cat" "Malformed integer literal"
run_check_fail "invalid binary digit" "$FIXTURE_DIR/revise_bad_integer_binary_digit.cat" "Malformed integer literal"
run_check_fail "invalid octal digit" "$FIXTURE_DIR/revise_bad_integer_octal_digit.cat" "Malformed integer literal"
run_check_fail "separator immediately after radix prefix" "$FIXTURE_DIR/revise_bad_integer_radix_separator.cat" "Malformed integer literal"
run_check_fail "repeated decimal separators" "$FIXTURE_DIR/revise_bad_integer_repeated_separator.cat" "Malformed integer literal"
run_check_fail "trailing decimal separator" "$FIXTURE_DIR/revise_bad_integer_trailing_separator.cat" "Malformed integer literal"
run_check_fail "unknown suffix-like radix tail is not accepted" "$FIXTURE_DIR/revise_bad_integer_unknown_suffix.cat" "Malformed integer literal"
run_check_fail "inferred integer exceeding Int32 default" "$FIXTURE_DIR/revise_bad_integer_inferred_int32_overflow.cat" "does not fit target type Int32"
run_check_fail "positive Int8 radix overflow" "$FIXTURE_DIR/revise_bad_integer_int8_overflow.cat" "does not fit target type Int8"
run_check_fail "negative Int8 radix underflow" "$FIXTURE_DIR/revise_bad_integer_int8_underflow.cat" "does not fit target type Int8"
run_check_fail "inferred integer exceeding Int32 inside pick value" "$FIXTURE_DIR/revise_bad_integer_inferred_pick_overflow.cat" "does not fit target type Int32"
run_check_fail "UInt128 radix overflow" "$FIXTURE_DIR/revise_bad_integer_uint128_overflow.cat" "does not fit target type UInt128"
run_check_fail "inferred integer exceeding Int32 inside if value" "$FIXTURE_DIR/revise_bad_integer_inferred_branch_overflow.cat" "does not fit target type Int32"
run_check_fail "inferred integer exceeding Int32 inside tuple" "$FIXTURE_DIR/revise_bad_integer_inferred_tuple_overflow.cat" "does not fit target type Int32"
run_check_fail "repeated float separators" "$FIXTURE_DIR/revise_bad_float_repeated_separator.cat" "Malformed float literal"
run_check_pass "integer-to-Char accepts scalar boundaries and rejects no valid values" \
  "$FIXTURE_DIR/revise_integer_char_scalar_boundaries.cat"
run_check_fail "typed Char rejects surrogate integer literals" \
  "$FIXTURE_DIR/revise_bad_integer_char_typed_surrogate.cat" \
  "does not fit target type Char."
run_check_fail "_Char suffix rejects surrogate integer literals" \
  "$FIXTURE_DIR/revise_bad_integer_char_suffix_surrogate.cat" \
  "does not fit target type Char."
run_check_fail "static Char rejects surrogate integer initializers" \
  "$FIXTURE_DIR/revise_bad_integer_char_static_surrogate.cat" \
  "does not fit target type Char."
run_check_fail "integer-to-Char rejects values above U+10FFFF" \
  "$FIXTURE_DIR/revise_bad_integer_char_out_of_range.cat" \
  "does not fit target type Char."
run_check_pass "PRD §11 Char Unicode scalars, UTF-8, escapes, and scalar boundaries" \
  "$FIXTURE_DIR/revise_char_literals.cat"
run_check_fail "empty character literal" \
  "$FIXTURE_DIR/revise_bad_char_empty.cat" \
  "Character literal must contain exactly one Unicode scalar value."
run_check_fail "multiple-scalar character literal" \
  "$FIXTURE_DIR/revise_bad_char_multiple.cat" \
  "Character literal must contain exactly one Unicode scalar value."
run_check_fail "unknown character escape" \
  "$FIXTURE_DIR/revise_bad_char_escape.cat" \
  "Unknown escape sequence in character literal."
run_check_fail "malformed Unicode character escape" \
  "$FIXTURE_DIR/revise_bad_char_unicode_escape.cat" \
  "Malformed Unicode escape in character literal."
run_check_fail "surrogate character escape" \
  "$FIXTURE_DIR/revise_bad_char_surrogate.cat" \
  "Unicode escape is not a valid Unicode scalar value."
run_check_fail "character escape above U+10FFFF" \
  "$FIXTURE_DIR/revise_bad_char_out_of_range.cat" \
  "Unicode escape is not a valid Unicode scalar value."
run_check_fail "unterminated character literal" \
  "$FIXTURE_DIR/revise_bad_char_unterminated.cat" \
  "Unterminated character literal."
while read -r invalid_utf8_case invalid_utf8_hex; do
  char_invalid_utf8="$TEMP_DIR/revise_bad_char_invalid_utf8_${invalid_utf8_case}.cat"
  python3 - "$char_invalid_utf8" "$invalid_utf8_hex" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
raw_bytes = bytes.fromhex(sys.argv[2])
path.write_bytes(b"fn main() {\n    val bad = '" + raw_bytes + b"'\n}\n")
PY
  run_check_fail "invalid UTF-8 character literal ($invalid_utf8_case)" \
    "$char_invalid_utf8" \
    "Invalid UTF-8 in character literal."
done <<'EOF'
illegal_lead ff
overlong_two_byte c080
overlong_three_byte e08080
truncated_two_byte c2
truncated_three_byte e282
invalid_continuation e228a1
surrogate_encoding eda080
above_unicode_max f4908080
EOF
char_span_output=""
if char_span_output="$("$CLAW_EXE" check "$FIXTURE_DIR/revise_bad_char_empty.cat" 2>&1)"; then
  echo "empty character literal unexpectedly passed span check" >&2
  exit 1
fi
if [[ "$char_span_output" != *"revise_bad_char_empty.cat:2:15"* ]]; then
  echo "character-literal diagnostic did not preserve its source span:" >&2
  echo "$char_span_output" >&2
  exit 1
fi
char_air_output="$("$CLAW_EXE" air "$FIXTURE_DIR/revise_char_literals.cat")"
for expected_char_value in "65 : Char" "233 : Char" "128512 : Char" "1114111 : Char"; do
  if [[ "$char_air_output" != *"$expected_char_value"* ]]; then
    echo "Char AIR did not preserve scalar value/type '$expected_char_value'" >&2
    echo "$char_air_output" >&2
    exit 1
  fi
done

run_check_pass "Str Range[Int32] indexing, get Maybe provenance, and UTF-8 boundaries" \
  "$SCRIPT_DIR/../native/fixtures/revise_str_range_views.cat"
run_check_fail "legacy public Str.slice(start,length) is removed" \
  "$FIXTURE_DIR/revise_bad_str_legacy_slice.cat" "Type 'Str' does not provide method 'slice'."
run_check_fail "Maybe[ref Str] from Str.get cannot escape a local receiver" \
  "$FIXTURE_DIR/revise_bad_str_range_local_return.cat" \
  "Borrowed-returning functions must return a view derived from one of their borrowed parameters."
