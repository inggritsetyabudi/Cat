#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLAW_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
FIXTURE_DIR="$CLAW_DIR/tests"
GOLDEN_DIR="$SCRIPT_DIR/golden"
OIR_FIXTURE_DIR="$SCRIPT_DIR/fixtures"

if [[ -z "${CLAW_EXE:-}" ]]; then
  if [[ -x "$CLAW_DIR/build/claw" ]]; then
    CLAW_EXE="$CLAW_DIR/build/claw"
  elif [[ -x "$CLAW_DIR/build-ucrt64-clang/claw.exe" ]]; then
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw.exe"
  else
    CLAW_EXE="$CLAW_DIR/build-ucrt64-clang/claw-codex.exe"
  fi
fi

if [[ ! -x "$CLAW_EXE" ]]; then
  echo "missing compiler executable: $CLAW_EXE" >&2
  exit 1
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

run_oir_golden() {
  local label="$1"
  local fixture="$2"
  local expected="$GOLDEN_DIR/$label.oir"
  local actual="$TEMP_DIR/$label.oir"

  echo "[oir/pass] $label"
  if ! "$CLAW_EXE" oir "$fixture" > "$actual"; then
    echo "OIR emission failed for $fixture" >&2
    exit 1
  fi
  if ! cmp -s "$expected" "$actual"; then
    echo "OIR output changed for $label (expected byte-for-byte match)" >&2
    diff -u --label "$expected" --label "$actual" "$expected" "$actual" || true
    exit 1
  fi
}

run_oir_fail() {
  local label="$1"
  local fixture="$2"
  local expect="$3"
  echo "[oir/fail] $label"
  local output
  if output="$("$CLAW_EXE" oir "$fixture" 2>&1)"; then
    echo "expected OIR generation for $label to fail, but it passed" >&2
    exit 1
  fi
  if [[ "$output" != *"$expect"* ]]; then
    echo "$label OIR did not report expected diagnostic substring: $expect" >&2
    echo "$output" >&2
    exit 1
  fi
}

run_oir_golden "operator" "$FIXTURE_DIR/native/fixtures/revise_operator_overload.cat"
run_oir_golden "error" "$FIXTURE_DIR/frontend/fixtures/revise_error_handling.cat"
run_oir_golden "try-contract" "$FIXTURE_DIR/native/fixtures/revise_try_contract.cat"
run_oir_golden "try-owned-result" "$FIXTURE_DIR/native/fixtures/revise_try_owned_result.cat"
run_oir_golden "try-error-alias" "$FIXTURE_DIR/frontend/fixtures/revise_try_error_alias"
run_oir_golden "choice-resolution" "$FIXTURE_DIR/frontend/fixtures/revise_choice_resolution.cat"
run_oir_golden "anchor-choice" "$FIXTURE_DIR/native/fixtures/revise_anchor_choice.cat"
run_oir_golden "scope" "$FIXTURE_DIR/frontend/fixtures/revise_scope_refs.cat"
run_oir_golden "workspace" "$FIXTURE_DIR/frontend/fixtures/revise_nested_scoped_workspace"
run_oir_golden "if-pick-values" "$FIXTURE_DIR/native/fixtures/revise_if_pick_values.cat"
run_oir_golden "tuples" "$FIXTURE_DIR/native/fixtures/revise_tuples.cat"
run_oir_golden "int32-range" "$FIXTURE_DIR/native/fixtures/revise_int32_range.cat"
run_oir_golden "int32-range-scan" "$FIXTURE_DIR/native/fixtures/revise_int32_range_scan.cat"
run_oir_golden "fixed-array" "$OIR_FIXTURE_DIR/fixed-array.cat"
run_oir_golden "fixed-array-uint8" "$OIR_FIXTURE_DIR/fixed-array-uint8.cat"
run_oir_golden "fixed-array-index-assignment" "$FIXTURE_DIR/native/fixtures/revise_fixed_array_index_assignment.cat"
run_oir_fail "fixed-array byte-size overflow is rejected before OIR emission" \
  "$FIXTURE_DIR/frontend/fixtures/revise_bad_fixed_array_size_overflow.cat" \
  "Fixed Array byte size overflows the target size limit"
run_oir_golden "char-range-scan" "$OIR_FIXTURE_DIR/char-range-scan.cat"
run_oir_golden "uint8-range-scan" "$OIR_FIXTURE_DIR/uint8-range-scan.cat"
run_oir_golden "int8-range-scan" "$OIR_FIXTURE_DIR/int8-range-scan.cat"
run_oir_golden "int16-range-scan" "$FIXTURE_DIR/native/fixtures/revise_int16_range.cat"
run_oir_golden "wide-integer-ranges" "$FIXTURE_DIR/native/fixtures/revise_wide_integer_ranges.cat"
run_oir_golden "int128-uint128-ranges" "$FIXTURE_DIR/native/fixtures/revise_int128_uint128_ranges.cat"
run_oir_golden "tuple-destructuring" "$FIXTURE_DIR/native/fixtures/revise_tuple_destructuring.cat"
run_oir_golden "tuple-nested-destructuring" "$FIXTURE_DIR/native/fixtures/revise_tuple_nested_destructuring.cat"
run_oir_golden "tuple-owned-destructuring" "$FIXTURE_DIR/native/fixtures/revise_tuple_owned_destructuring.cat"
run_oir_golden "tuple-pick" "$FIXTURE_DIR/native/fixtures/revise_tuple_pick.cat"
run_oir_golden "implicit-return" "$FIXTURE_DIR/native/fixtures/revise_implicit_return.cat"
run_oir_golden "discarded-anchor-temporary" "$FIXTURE_DIR/native/fixtures/revise_discarded_anchor_temporary.cat"
run_oir_golden "discarded-binary-anchor-temporary" "$FIXTURE_DIR/native/fixtures/revise_discarded_binary_anchor_temporary.cat"
run_oir_fail "middle expression cannot satisfy return" "$FIXTURE_DIR/frontend/fixtures/revise_bad_implicit_return.cat" "must end with a value of type Int32 or use explicit return."
run_oir_fail "mixed Char and Int32 range endpoints are rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_mixed_bounds.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "Int16 and Int8 range bounds cannot be mixed" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int16_range_mixed_bounds.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "Int8 membership rejects Range[Int16]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int8_in_int16_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Int16 membership rejects Range[Int8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int16_in_int8_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "indexed scans reject Str" "$FIXTURE_DIR/frontend/fixtures/revise_bad_indexed_scan_str.cat" "An indexed scan is supported only over supported Range values or fixed Array[Int32, N] and Array[UInt8, N]."
run_oir_fail "Str slicing remains restricted to Range[Int32]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_str_char_range_index.cat" "Index expression must have type USize, got Range[Char]"
run_oir_fail "Str slicing remains restricted to Range[Int32] for Int16" "$FIXTURE_DIR/frontend/fixtures/revise_bad_str_int16_range_index.cat" "Index expression must have type USize, got Range[Int16]"
run_oir_fail "Str slicing remains restricted to Range[Int32] for Int64" "$FIXTURE_DIR/frontend/fixtures/revise_bad_str_int64_range_index.cat" "Index expression must have type USize, got Range[Int64]"
run_oir_fail "floating-point range endpoints remain unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int32_range_float.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char"
run_oir_fail "Range[Int128]/Range[UInt128] bounds reject mixed types" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_int128_uint128_mixed_bounds.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "Int128 membership rejects Range[UInt128]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int128_in_uint128_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "UInt128 membership rejects Range[Int128]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_uint128_in_int128_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Str slicing remains restricted to Range[Int32] for Int128" "$FIXTURE_DIR/frontend/fixtures/revise_bad_str_int128_range_index.cat" "Index expression must have type USize, got Range[Int128]"
run_oir_fail "wide range bounds reject mixed Int64 and UInt64 types" "$FIXTURE_DIR/frontend/fixtures/revise_bad_wide_range_mixed_bounds.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "UInt64 membership rejects Range[USize] despite matching LLVM width" "$FIXTURE_DIR/frontend/fixtures/revise_bad_uint64_in_usize_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "UInt8 and Int32 range bounds cannot be mixed" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_mixed_uint8_int32.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "Int8 and UInt8 range bounds cannot be mixed" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int8_range_uint8_bounds.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "Int8 and Int32 range bounds cannot be mixed" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int8_range_int32_bounds.cat" "Range bounds currently support only Int8, Int16, Int32, Int64, Int128, UInt8, UInt16, UInt32, UInt64, UInt128, USize, or Char with matching types"
run_oir_fail "UInt8 membership requires Range[UInt8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_mixed_uint8_membership.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "UInt8 membership rejects Range[Int8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_uint8_in_int8_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Int8 membership rejects Range[UInt8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int8_in_uint8_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Int8 membership rejects Range[Int32]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int8_in_int32_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Int32 membership rejects Range[Int8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int32_in_int8_range.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Str slicing rejects Range[UInt8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_str_uint8_range_index.cat" "Index expression must have type USize, got Range[UInt8]"
run_oir_fail "membership requires a Range right operand" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_membership_rhs.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "membership rejects a non-scalar left operand" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_membership_left.cat" 'Operator `in` requires a matching Int8/Range[Int8], Int16/Range[Int16], Int32/Range[Int32], Int64/Range[Int64], Int128/Range[Int128], UInt8/Range[UInt8], UInt16/Range[UInt16], UInt32/Range[UInt32], UInt64/Range[UInt64], UInt128/Range[UInt128], USize/Range[USize], or Char/Range[Char] pair'
run_oir_fail "Str slicing rejects Range[Int8]" "$FIXTURE_DIR/frontend/fixtures/revise_bad_str_int8_range_index.cat" "Index expression must have type USize, got Range[Int8]"
run_oir_fail "range operators are non-associative" "$FIXTURE_DIR/frontend/fixtures/revise_bad_int32_range_chain.cat" 'Range operators `..` and `..=` are non-associative'
run_oir_fail "range comparisons are unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_range_comparison.cat" 'Range values support `in` membership only'
run_oir_fail "trailing decimal dot remains a single-dot projection" "$FIXTURE_DIR/frontend/fixtures/revise_bad_trailing_decimal_dot.cat" "Type 'IntLiteral' does not expose fields."
run_oir_fail "tuple projection index out of range" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_projection.cat" "Tuple projection index 2 is out of range"
run_oir_fail "nested tuple projection assignment remains unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_nested_tuple_assignment.cat" "Member assignment requires mutable storage or a ref mut view."
run_oir_fail "tuple field without a safe destructor remains rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_field.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_oir_fail "tuple literal expected arity mismatch" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_arity.cat" "Tuple literal has 1 element(s), but the expected tuple type has 2."
run_oir_fail "nested tuple patterns require tuple-typed fields" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring.cat" "Nested tuple destructuring pattern at element 0 requires a tuple value, got Int32."
run_oir_fail "nested tuple destructuring pattern arity mismatch" "$FIXTURE_DIR/frontend/fixtures/revise_bad_nested_tuple_destructuring_arity.cat" "Tuple destructuring pattern at element 0 expects 2 binding(s), but the tuple has 1 field(s)."
run_oir_fail "nested tuple destructuring rejects owned tuple values" "$FIXTURE_DIR/frontend/fixtures/revise_bad_nested_tuple_destructuring_owned.cat" "Nested tuple destructuring currently supports only plain, concrete, non-view tuple values"
run_oir_fail "tuple destructuring pattern arity mismatch" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_arity.cat" "Tuple destructuring expects 2 binding(s), but the tuple has 3 field(s)."
run_oir_fail "tuple destructuring requires a tuple value" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_non_tuple.cat" "Tuple destructuring requires a tuple value"
run_oir_fail "tuple destructuring consumes the source tuple" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_reuse.cat" "Use of moved value -> pair"
run_oir_fail "tuple destructuring of a view tuple remains unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_view.cat" "cannot destructure a view tuple"
run_oir_fail "tuple view fields remain unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_view_field.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_oir_fail "generic tuple fields remain unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_generic.cat" "Tuple fields must be concrete plain values or Anchor[T] with concrete plain T"
run_oir_fail "Span borrow-carrier tuple fields remain unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_span.cat" "Tuple destructuring currently supports plain fields and Anchor[T] fields with plain T; element 0 has type Span[Int32]."
run_oir_fail "mutable tuple destructuring remains unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_destructuring_mutable.cat" 'Tuple destructuring is currently supported only for `val` bindings.'
run_oir_fail "tuple pick pattern requires a tuple selector" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_pick_pattern.cat" "Tuple pick pattern at value requires a tuple value"
run_oir_fail "tuple pick-expression pattern requires a tuple selector" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_pick_expression_pattern.cat" "Tuple pick pattern at value requires a tuple value"
run_oir_fail "owned tuple alias cannot reuse moved source" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_alias.cat" "Use of moved value -> pair"
run_oir_fail "owned tuple field cannot be moved through projection" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_projection_move.cat" "Cannot move an owned tuple field through numeric projection"
run_oir_fail "owned tuple field cannot be replaced directly" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_field_replace.cat" "Replacing an owned tuple field directly is unsupported"
run_oir_fail "nested owned tuple projection cannot be partially destructured" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_nested_projection.cat" "Cannot destructure an owned tuple projection"
run_oir_fail "owned tuple pick patterns remain unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_pick.cat" "Tuple pick patterns currently support only plain, concrete, non-view fields"
run_oir_fail "pick cannot leak a temporary owned tuple through wildcard" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_pick_temporary.cat" "Pick over a temporary owned tuple is unsupported"
run_oir_fail "owned-field tuple cannot be embedded in a shape" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_shape_field.cat" "Shape fields cannot contain tuples with owned fields"
run_oir_fail "owned-field tuple cannot be embedded in a generic container" "$FIXTURE_DIR/frontend/fixtures/revise_bad_tuple_owned_container.cat" "Tuples with owned fields are supported only as direct tuple values"
run_oir_fail "owned temporary without destructor is rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_discarded_owned_temporary.cat" 'Cannot discard owned temporary of type `Str` because no safe destructor is available.'
run_oir_fail "nested Anchor payload requires recursive destruction" "$FIXTURE_DIR/frontend/fixtures/revise_bad_anchor_nested_payload.cat" 'Anchor.new(...) cannot store payload type `Anchor[Int32]` because it contains a nested Anchor'
run_oir_fail "nested Anchor payload type is unsupported" "$FIXTURE_DIR/frontend/fixtures/revise_bad_anchor_nested_type.cat" "is unsupported because an Anchor payload cannot contain another Anchor"
run_oir_fail "generic Shape specialization cannot nest Anchor payloads" "$FIXTURE_DIR/frontend/fixtures/revise_bad_anchor_nested_shape_specialization.cat" 'is unsupported because an Anchor payload cannot contain another Anchor'
run_oir_fail "Anchor.new cannot wrap a Shape with an Anchor field" "$FIXTURE_DIR/frontend/fixtures/revise_bad_anchor_nested_shape_payload.cat" 'is unsupported because an Anchor payload cannot contain another Anchor'
run_oir_fail "discarded ShapeInit without destructor is rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_discarded_shape_init.cat" 'Cannot discard owned temporary of type `Record` because no safe destructor is available.'
run_oir_fail "member projection from owned temporary is rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_discarded_member_temporary.cat" "Cannot discard an expression projected from an owned temporary"
run_oir_fail "index projection from owned temporary is rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_discarded_index_temporary.cat" "Cannot discard an expression projected from an owned temporary"
run_oir_fail "borrowing method on owned temporary is rejected" "$FIXTURE_DIR/frontend/fixtures/revise_bad_discarded_borrowed_receiver.cat" "Cannot discard a call on an owned temporary receiver"
run_oir_fail "missing else in if value" "$FIXTURE_DIR/frontend/fixtures/revise_bad_if_value_missing_else.cat" "'if' as a value requires an 'else' branch."
run_oir_fail "incompatible if branch values" "$FIXTURE_DIR/frontend/fixtures/revise_bad_if_value_incompatible.cat" "Incompatible branch types in if expression"
run_oir_fail "incompatible pick branch values" "$FIXTURE_DIR/frontend/fixtures/revise_bad_pick_value_incompatible.cat" "Branch type mismatch in pick expression: expected Int32, got Str."
run_oir_fail "non-exhaustive pick value" "$FIXTURE_DIR/frontend/fixtures/revise_bad_pick_value_non_exhaustive.cat" "Non-exhaustive pick for choice type 'Route'."
run_oir_fail "wildcard must be last in pick value" "$FIXTURE_DIR/frontend/fixtures/revise_bad_pick_value_wildcard_nonfinal.cat" "The '_' catch-all branch must be the last branch in a pick."
run_oir_fail "branch after wildcard is unreachable" "$FIXTURE_DIR/frontend/fixtures/revise_bad_pick_value_wildcard_nonfinal.cat" "Unreachable pick branch after the '_' catch-all branch."
run_oir_fail "try in return expression" "$FIXTURE_DIR/frontend/fixtures/revise_bad_try_return_expr.cat" "Expected expression."
run_oir_fail "shorthand try error-type mismatch" "$FIXTURE_DIR/frontend/fixtures/revise_bad_try_error_type.cat" '`try` shorthand error type mismatch: expected Int32, got Str'
run_oir_fail "nonterminal try else block" "$FIXTURE_DIR/frontend/fixtures/revise_bad_try_else_nonterminal.cat" '`try ... else` failure block must terminate the current control path.'
run_oir_fail "owned Result reuse after try" "$FIXTURE_DIR/frontend/fixtures/revise_bad_try_owned_reuse.cat" 'Use of moved value -> result'
run_oir_fail "try error alias generic mismatch" "$FIXTURE_DIR/frontend/fixtures/revise_bad_try_alias_generic_mismatch" '`try` shorthand error type mismatch'
run_oir_fail "same nominal spelling in different modules" "$FIXTURE_DIR/frontend/fixtures/revise_bad_try_alias_cross_module" '`try` shorthand error type mismatch'

echo "All OIR golden tests passed."
