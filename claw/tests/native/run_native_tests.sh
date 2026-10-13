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
implicit_return_output="$ARTIFACT_DIR/revise_implicit_return.exe"
discarded_anchor_output="$ARTIFACT_DIR/revise_discarded_anchor_temporary.exe"
discarded_binary_anchor_output="$ARTIFACT_DIR/revise_discarded_binary_anchor_temporary.exe"
tuples_output="$ARTIFACT_DIR/revise_tuples.exe"
tuple_destructuring_output="$ARTIFACT_DIR/revise_tuple_destructuring.exe"
tuple_owned_output="$ARTIFACT_DIR/revise_tuple_owned_destructuring.exe"
tuple_pick_output="$ARTIFACT_DIR/revise_tuple_pick.exe"
range_output="$ARTIFACT_DIR/revise_int32_range.exe"
char_range_output="$ARTIFACT_DIR/revise_char_range.exe"
uint8_range_output="$ARTIFACT_DIR/revise_uint8_range.exe"
int8_range_output="$ARTIFACT_DIR/revise_int8_range.exe"
int8_print_output="$ARTIFACT_DIR/revise_int8_print.exe"
uint8_print_output="$ARTIFACT_DIR/revise_uint8_print.exe"
range_scan_output="$ARTIFACT_DIR/revise_int32_range_scan.exe"
range_scan_shadow_output="$ARTIFACT_DIR/revise_int32_range_scan_shadowing.exe"
local_shadowing_output="$ARTIFACT_DIR/revise_local_shadowing_binding_identity.exe"
builtin_receiver_shadowing_output="$ARTIFACT_DIR/revise_builtin_member_receiver_shadowing.exe"
str_bounds_shadowing_output="$ARTIFACT_DIR/revise_str_bounds_shadowing.exe"
try_result_shadowing_output="$ARTIFACT_DIR/revise_try_result_shadowing.exe"
pick_payload_shadowing_output="$ARTIFACT_DIR/revise_pick_payload_shadowing.exe"
pick_anchor_payload_shadowing_output="$ARTIFACT_DIR/revise_pick_anchor_payload_shadowing.exe"
range_reject_output="$ARTIFACT_DIR/revise_int32_range_reject.exe"
indexed_scan_reject_output="$ARTIFACT_DIR/revise_indexed_scan_reject.exe"
numeric_literals_output="$ARTIFACT_DIR/revise_numeric_literals.exe"
radix_literals_output="$ARTIFACT_DIR/revise_integer_radix_literals.exe"
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
short_circuit_ownership_output="$ARTIFACT_DIR/revise_operator_short_circuit_ownership.exe"
float_unary_output="$ARTIFACT_DIR/revise_float64_unary_negation.exe"
int32_min_output="$ARTIFACT_DIR/revise_int32_min_literal.exe"

rm -f "$single_output" "${single_output%.exe}.ll" \
      "$implicit_return_output" "${implicit_return_output%.exe}.ll" \
      "$discarded_anchor_output" "${discarded_anchor_output%.exe}.ll" \
      "$discarded_binary_anchor_output" "${discarded_binary_anchor_output%.exe}.ll" \
      "$tuples_output" "${tuples_output%.exe}.ll" \
      "$tuple_destructuring_output" "${tuple_destructuring_output%.exe}.ll" \
      "$tuple_owned_output" "${tuple_owned_output%.exe}.ll" \
      "$tuple_pick_output" "${tuple_pick_output%.exe}.ll" \
      "$range_output" "${range_output%.exe}.ll" \
      "$char_range_output" "${char_range_output%.exe}.ll" \
      "$uint8_range_output" "${uint8_range_output%.exe}.ll" \
      "$int8_range_output" "${int8_range_output%.exe}.ll" \
      "$int8_print_output" "${int8_print_output%.exe}.ll" \
      "$uint8_print_output" "${uint8_print_output%.exe}.ll" \
      "$range_scan_output" "${range_scan_output%.exe}.ll" \
      "$range_scan_shadow_output" "${range_scan_shadow_output%.exe}.ll" \
      "$local_shadowing_output" "${local_shadowing_output%.exe}.ll" \
      "$builtin_receiver_shadowing_output" "${builtin_receiver_shadowing_output%.exe}.ll" \
      "$str_bounds_shadowing_output" "${str_bounds_shadowing_output%.exe}.ll" \
      "$try_result_shadowing_output" "${try_result_shadowing_output%.exe}.ll" \
      "$pick_payload_shadowing_output" "${pick_payload_shadowing_output%.exe}.ll" \
      "$pick_anchor_payload_shadowing_output" "${pick_anchor_payload_shadowing_output%.exe}.ll" \
      "$range_reject_output" "${range_reject_output%.exe}.ll" \
      "$indexed_scan_reject_output" "${indexed_scan_reject_output%.exe}.ll" \
      "$numeric_literals_output" "${numeric_literals_output%.exe}.ll" \
      "$radix_literals_output" "${radix_literals_output%.exe}.ll" \
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
      "$nested_scoped_workspace_output" "${nested_scoped_workspace_output%.exe}.ll" \
      "$short_circuit_ownership_output" "${short_circuit_ownership_output%.exe}.ll" \
      "$float_unary_output" "${float_unary_output%.exe}.ll" \
      "$int32_min_output" "${int32_min_output%.exe}.ll"

echo "[build/pass] test_native/revise_single_file.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_single_file.cat" "$single_output" >/dev/null
expect_generated_ll "$single_output"
single_stdout="$(normalize_stdout "$single_output")"
if [[ "$single_stdout" != "3" ]]; then
  echo "revised single-file native build produced unexpected output: $single_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_implicit_return.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_implicit_return.cat" "$implicit_return_output" >/dev/null
expect_generated_ll "$implicit_return_output"
implicit_return_stdout="$(normalize_stdout "$implicit_return_output")"
if [[ "$implicit_return_stdout" != "42" ]]; then
  echo "implicit-return native build produced unexpected output: $implicit_return_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_discarded_anchor_temporary.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_discarded_anchor_temporary.cat" "$discarded_anchor_output" >/dev/null
expect_generated_ll "$discarded_anchor_output"
discarded_anchor_stdout="$(normalize_stdout "$discarded_anchor_output")"
expected_discarded_anchor_stdout=$'payload-once\nfinished'
if [[ "$discarded_anchor_stdout" != "$expected_discarded_anchor_stdout" ]]; then
  echo "discarded Anchor temporary output was unexpected: $discarded_anchor_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_discarded_binary_anchor_temporary.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_discarded_binary_anchor_temporary.cat" "$discarded_binary_anchor_output" >/dev/null
expect_generated_ll "$discarded_binary_anchor_output"
discarded_binary_anchor_stdout="$(normalize_stdout "$discarded_binary_anchor_output")"
expected_discarded_binary_anchor_stdout=$'left-once\nright-once\nmethod-once\nif-once\npick-once\nfinished'
if [[ "$discarded_binary_anchor_stdout" != "$expected_discarded_binary_anchor_stdout" ]]; then
  echo "discarded BinaryExpr Anchor output was unexpected: $discarded_binary_anchor_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_tuples.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_tuples.cat" "$tuples_output" >/dev/null
expect_generated_ll "$tuples_output"
tuples_stdout="$(normalize_stdout "$tuples_output")"
expected_tuples_stdout=$'42\n42\n42\n42\n42\n42\n42\nfloat-ok'
if [[ "$tuples_stdout" != "$expected_tuples_stdout" ]]; then
  echo "tuple native output was unexpected:" >&2
  printf '%s\n' "$tuples_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_tuple_destructuring.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_tuple_destructuring.cat" "$tuple_destructuring_output" >/dev/null
expect_generated_ll "$tuple_destructuring_output"
tuple_destructuring_stdout="$(normalize_stdout "$tuple_destructuring_output")"
expected_tuple_destructuring_stdout=$'1\n2\n3\n5\n6\n6\n7\n7'
if [[ "$tuple_destructuring_stdout" != "$expected_tuple_destructuring_stdout" ]]; then
  echo "tuple destructuring native output was unexpected:" >&2
  printf '%s\n' "$tuple_destructuring_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_tuple_owned_destructuring.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_tuple_owned_destructuring.cat" "$tuple_owned_output" >/dev/null
expect_generated_ll "$tuple_owned_output"
tuple_owned_stdout="$(normalize_stdout "$tuple_owned_output")"
expected_tuple_owned_stdout=$'factory\n11\n22\nafter-multi-wildcard\nfactory\n11\n22\n11\n22\nfactory\n11\n22\n22\n22\nfactory\n11\n22\n11\n0\n31\n32\n32\n41\n42\n43\nfactory\n11\n22\n22'
if [[ "$tuple_owned_stdout" != "$expected_tuple_owned_stdout" ]]; then
  echo "owned tuple destructuring native output was unexpected:" >&2
  printf '%s\n' "$tuple_owned_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_tuple_pick.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_tuple_pick.cat" "$tuple_pick_output" >/dev/null
expect_generated_ll "$tuple_pick_output"
tuple_pick_stdout="$(normalize_stdout "$tuple_pick_output")"
expected_tuple_pick_stdout=$'selector-once\n18\nselector-once\n19\n0\n7'
if [[ "$tuple_pick_stdout" != "$expected_tuple_pick_stdout" ]]; then
  echo "tuple pick native output was unexpected:" >&2
  printf '%s\n' "$tuple_pick_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_int32_range.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_int32_range.cat" "$range_output" >/dev/null
expect_generated_ll "$range_output"
range_stdout="$(normalize_stdout "$range_output")"
expected_range_stdout=$'10\n1\n1\n1\n1\nstart\nend\n0\n1\n1\n0\n1\n1\n0\n0\n0\n0\n1\n1\n1\n0\n1'
if [[ "$range_stdout" != "$expected_range_stdout" ]]; then
  echo "Int32 range native output was unexpected:" >&2
  printf '%s\n' "$range_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_uint8_range.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_uint8_range.cat" "$uint8_range_output" >/dev/null
expect_generated_ll "$uint8_range_output"
uint8_range_ll="${uint8_range_output%.exe}.ll"
if ! grep -q 'icmp uge i8' "$uint8_range_ll" ||
   ! grep -q 'icmp ult i8' "$uint8_range_ll" ||
   ! grep -q 'icmp ule i8' "$uint8_range_ll" ||
   ! grep -q 'add i8' "$uint8_range_ll" ||
   ! grep -q 'add i64' "$uint8_range_ll"; then
  echo "UInt8 range LLVM IR omitted unsigned bounds or USize index advancement" >&2
  exit 1
fi
uint8_range_stdout="$(timeout 10s "$uint8_range_output" | tr -d '\r')"
expected_uint8_range_stdout=$'1\n1\n0\n0\n1\n0\n1\n0\n0\nmembership-start\nmembership-end\n1\n1\n2\n3\n254\n254\n0\n255\n1\n7\nscan-start\nscan-end\n254\n255\n255\n0\n255\n256'
if [[ "$uint8_range_stdout" != "$expected_uint8_range_stdout" ]]; then
  echo "UInt8 range native output was unexpected:" >&2
  printf '%s\n' "$uint8_range_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_int8_range.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_int8_range.cat" "$int8_range_output" >/dev/null
expect_generated_ll "$int8_range_output"
int8_range_ll="${int8_range_output%.exe}.ll"
if ! grep -q 'icmp sge i8' "$int8_range_ll" ||
   ! grep -q 'icmp slt i8' "$int8_range_ll" ||
   ! grep -q 'icmp sle i8' "$int8_range_ll" ||
   ! grep -q 'icmp eq i8' "$int8_range_ll" ||
   ! grep -q 'add i8' "$int8_range_ll" ||
   ! grep -q 'add i64' "$int8_range_ll" ||
   grep -Eq 'icmp (uge|ult|ule) i8' "$int8_range_ll"; then
  echo "Int8 range LLVM IR omitted signed bounds, safe narrow iteration, or USize index advancement" >&2
  exit 1
fi
int8_range_stdout="$(timeout 10s "$int8_range_output" | tr -d '\r')"
expected_int8_range_stdout=$'1\n0\n1\n1\n1\n1\n0\n-2\nordinary-negative\n-1\n0\nordinary-zero\n1\nordinary-positive\n4\n-1\n0\nindex-zero-value\n0\n1\nindex-one-value\n1\n2\nindex-two-value\nempty-exclusive\nempty-reversed\nequal-inclusive\nequal-item\nstart-once\nend-once\n1\n-128\nminimum-edge\n127\nmaximum-edge\n256'
if [[ "$int8_range_stdout" != "$expected_int8_range_stdout" ]]; then
  echo "Int8 range native output was unexpected:" >&2
  printf '%s\n' "$int8_range_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_uint8_print.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_uint8_print.cat" "$uint8_print_output" >/dev/null
expect_generated_ll "$uint8_print_output"
uint8_print_stdout="$CWD_TEST_DIR/revise_uint8_print.stdout"
uint8_print_expected="$CWD_TEST_DIR/revise_uint8_print.expected"
printf '254255' > "$uint8_print_expected"
"$uint8_print_output" > "$uint8_print_stdout"
if ! cmp -s "$uint8_print_expected" "$uint8_print_stdout"; then
  echo "UInt8 print native output did not match the exact no-newline bytes '254255'" >&2
  od -An -tx1c "$uint8_print_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_int8_print.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_int8_print.cat" "$int8_print_output" >/dev/null
expect_generated_ll "$int8_print_output"
int8_print_stdout="$CWD_TEST_DIR/revise_int8_print.stdout"
int8_print_expected="$CWD_TEST_DIR/revise_int8_print.expected"
printf '%s\n' '-128,-1,0,127' > "$int8_print_expected"
"$int8_print_output" > "$int8_print_stdout"
if ! cmp -s "$int8_print_expected" "$int8_print_stdout"; then
  echo "Int8 print/println output did not match the exact signed edge bytes" >&2
  od -An -tx1c "$int8_print_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_char_range.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_char_range.cat" "$char_range_output" >/dev/null
expect_generated_ll "$char_range_output"
char_range_ll="${char_range_output%.exe}.ll"
if ! grep -q '55295' "$char_range_ll" || ! grep -q 'i32 57344' "$char_range_ll" || ! grep -q 'add i64' "$char_range_ll"; then
  echo "Char range LLVM IR omitted the Unicode scalar successor or USize index advancement" >&2
  exit 1
fi
char_range_stdout="$(timeout 10s "$char_range_output" | tr -d '\r')"
expected_char_range_stdout=$'1\n1\n0\n1\n0\n0\n0\n0\n1\n10\n11\n12\n13\n20\n21\n22\nscan-start\nscan-end\n30\n31\nmembership-start\nmembership-end\n1\n40\n41\n42\n43\n50\n51\n52\n1\n0'
if [[ "$char_range_stdout" != "$expected_char_range_stdout" ]]; then
  echo "Char range native output was unexpected:" >&2
  printf '%s\n' "$char_range_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_int32_range_scan.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_int32_range_scan.cat" "$range_scan_output" >/dev/null
expect_generated_ll "$range_scan_output"
range_scan_ll="${range_scan_output%.exe}.ll"
if ! grep -q 'icmp sle i32' "$range_scan_ll" || ! grep -q 'add i64' "$range_scan_ll"; then
  echo "Range scan LLVM IR omitted inclusive signed bounds or USize index advancement" >&2
  exit 1
fi
range_scan_stdout="$(timeout 10s "$range_scan_output" | tr -d '\r')"
expected_range_scan_stdout=$'1\n2\n3\n6\n7\nscan-start\nscan-end\n2\n3\n3\n100\n4\n101\n5\n102\n10\n100\n12\n102\n13\n199\nempty\nexclusive-max\n2147483646\n100\ninclusive-max\n2147483647\n100'
if [[ "$range_scan_stdout" != "$expected_range_scan_stdout" ]]; then
  echo "Range scan native output was unexpected:" >&2
  printf '%s\n' "$range_scan_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_int32_range_scan_shadowing.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_int32_range_scan_shadowing.cat" "$range_scan_shadow_output" >/dev/null
expect_generated_ll "$range_scan_shadow_output"
range_scan_shadow_stdout="$(timeout 10s "$range_scan_shadow_output" | tr -d '\r')"
expected_range_scan_shadow_stdout=$'shadow\n20\n0\n7\n1\n0\n7\n2\n1\n20\n0\n21\n1\n7\n1\n0\n7\n2\n1\n21\n1'
if [[ "$range_scan_shadow_stdout" != "$expected_range_scan_shadow_stdout" ]]; then
  echo "Nested Range scan shadowing output was unexpected:" >&2
  printf '%s\n' "$range_scan_shadow_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_local_shadowing_binding_identity.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_local_shadowing_binding_identity.cat" "$local_shadowing_output" >/dev/null
expect_generated_ll "$local_shadowing_output"
local_shadowing_stdout="$(timeout 10s "$local_shadowing_output" | tr -d '\r')"
expected_local_shadowing_stdout=$'8\n3\n8\n99\n70\n110\n7\n5\n99\n1\n2\n88\n99'
if [[ "$local_shadowing_stdout" != "$expected_local_shadowing_stdout" ]]; then
  echo "Ordinary local/scan shadowing native output was unexpected:" >&2
  printf '%s\n' "$local_shadowing_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_builtin_member_receiver_shadowing.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_builtin_member_receiver_shadowing.cat" "$builtin_receiver_shadowing_output" >/dev/null
expect_generated_ll "$builtin_receiver_shadowing_output"
builtin_receiver_shadowing_stdout="$(timeout 10s "$builtin_receiver_shadowing_output" | tr -d '\r')"
expected_builtin_receiver_shadowing_stdout=$'20\n99'
if [[ "$builtin_receiver_shadowing_stdout" != "$expected_builtin_receiver_shadowing_stdout" ]]; then
  echo "Builtin member receiver shadowing native output was unexpected: $builtin_receiver_shadowing_stdout" >&2
  exit 1
fi
echo "[build/pass] test_native/revise_str_bounds_shadowing.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_str_bounds_shadowing.cat" "$str_bounds_shadowing_output" >/dev/null
expect_generated_ll "$str_bounds_shadowing_output"
str_bounds_shadowing_stdout="$(timeout 10s "$str_bounds_shadowing_output" | tr -d '\r')"
expected_str_bounds_shadowing_stdout=$'89\n89\n89\nY'
if [[ "$str_bounds_shadowing_stdout" != "$expected_str_bounds_shadowing_stdout" ]]; then
  echo "Shadowed Str bounds-check native output was unexpected: $str_bounds_shadowing_stdout" >&2
  exit 1
fi
echo "[build/pass] test_native/revise_try_result_shadowing.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_try_result_shadowing.cat" "$try_result_shadowing_output" >/dev/null
expect_generated_ll "$try_result_shadowing_output"
try_result_shadowing_stdout="$(timeout 10s "$try_result_shadowing_output" | tr -d '\r')"
expected_try_result_shadowing_stdout=$'7\n99\ninner\n0'
if [[ "$try_result_shadowing_stdout" != "$expected_try_result_shadowing_stdout" ]]; then
  echo "Try-result shadowing native output was unexpected: $try_result_shadowing_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_pick_payload_shadowing.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_pick_payload_shadowing.cat" "$pick_payload_shadowing_output" >/dev/null
expect_generated_ll "$pick_payload_shadowing_output"
pick_payload_shadowing_stdout="$(timeout 10s "$pick_payload_shadowing_output" | tr -d '\r')"
expected_pick_payload_shadowing_stdout=$'7\n99\n8\n99\n12\n99\n14\n99'
if [[ "$pick_payload_shadowing_stdout" != "$expected_pick_payload_shadowing_stdout" ]]; then
  echo "Pick-payload shadowing native output was unexpected: $pick_payload_shadowing_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_pick_anchor_payload_shadowing.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_pick_anchor_payload_shadowing.cat" "$pick_anchor_payload_shadowing_output" >/dev/null
expect_generated_ll "$pick_anchor_payload_shadowing_output"
pick_anchor_payload_shadowing_stdout="$(timeout 10s "$pick_anchor_payload_shadowing_output" | tr -d '\r')"
expected_pick_anchor_payload_shadowing_stdout=$'inner\nouter\ninner\nouter'
if [[ "$pick_anchor_payload_shadowing_stdout" != "$expected_pick_anchor_payload_shadowing_stdout" ]]; then
  echo "Owned Anchor pick-payload shadowing native output was unexpected: $pick_anchor_payload_shadowing_stdout" >&2
  exit 1
fi

echo "[build/fail] mixed Char and Int32 range bounds are rejected before producing a native binary"
rm -f "$range_reject_output" "${range_reject_output%.exe}.ll"
if range_reject_diagnostic="$("$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_bad_range_mixed_bounds.cat" "$range_reject_output" 2>&1)"; then
  echo "mixed Char and Int32 range unexpectedly built a native executable" >&2
  exit 1
fi
if [[ "$range_reject_diagnostic" != *"Range bounds currently support only Int8, Int32, UInt8, or Char with matching types"* ]]; then
  echo "mixed range native build failed without the expected sema diagnostic" >&2
  echo "$range_reject_diagnostic" >&2
  exit 1
fi
if [[ -e "$range_reject_output" ]]; then
  echo "unsupported Char range left a native binary behind" >&2
  exit 1
fi

echo "[build/fail] indexed scan over Str is rejected before producing a native binary"
rm -f "$indexed_scan_reject_output" "${indexed_scan_reject_output%.exe}.ll"
if indexed_scan_reject_diagnostic="$("$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_bad_indexed_scan_str.cat" "$indexed_scan_reject_output" 2>&1)"; then
  echo "indexed scan over Str unexpectedly built a native executable" >&2
  exit 1
fi
if [[ "$indexed_scan_reject_diagnostic" != *"An indexed scan is currently supported only over Range[Int8], Range[Int32], Range[UInt8], or Range[Char]."* ]]; then
  echo "unsupported indexed scan build failed without the expected sema diagnostic" >&2
  echo "$indexed_scan_reject_diagnostic" >&2
  exit 1
fi
if [[ -e "$indexed_scan_reject_output" ]]; then
  echo "unsupported indexed scan left a native binary behind" >&2
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

echo "[build/pass] test_native/revise_integer_radix_literals.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_integer_radix_literals.cat" "$radix_literals_output" >/dev/null
expect_generated_ll "$radix_literals_output"
radix_literals_ll="${radix_literals_output%.exe}.ll"
if ! grep -Fq 'store i32 43981,' "$radix_literals_ll" ||
   ! grep -Fq 'store i8 255,' "$radix_literals_ll" ||
   ! grep -Fq 'store i16 170,' "$radix_literals_ll" ||
   ! grep -Fq 'store i16 493,' "$radix_literals_ll" ||
   ! grep -Fq 'store i128 340282366920938463463374607431768211455,' "$radix_literals_ll" ||
   ! grep -Fq 'sub i128 0, 170141183460469231731687303715884105728' "$radix_literals_ll"; then
  echo "radix integer LLVM constants were not normalized to their expected decimal values" >&2
  exit 1
fi
radix_literals_stdout="$(normalize_stdout "$radix_literals_output")"
expected_radix_literals_stdout=$'grouped-ok\nhex-separator-ok\nhex-suffix-ok\nbinary-ok\noctal-ok\nint-suffix-ok\nhex-float-suffix-ok\nfloat-separator-ok\nint128-min-ok\nuint128-max-ok'
if [[ "$radix_literals_stdout" != "$expected_radix_literals_stdout" ]]; then
  echo "radix integer native output did not match normalized values/types:" >&2
  echo "$radix_literals_stdout" >&2
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

echo "[build/fail] nested Anchor payload is rejected before native output"
nested_anchor_payload_output="$CWD_TEST_DIR/revise_bad_anchor_nested_payload.exe"
nested_anchor_payload_log="$CWD_TEST_DIR/revise_bad_anchor_nested_payload.log"
if "$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_bad_anchor_nested_payload.cat" "$nested_anchor_payload_output" >"$nested_anchor_payload_log" 2>&1; then
  echo "nested Anchor payload unexpectedly built successfully" >&2
  exit 1
fi
if [[ -e "$nested_anchor_payload_output" ]]; then
  echo "nested Anchor payload rejection left a native binary behind: $nested_anchor_payload_output" >&2
  exit 1
fi
if ! grep -Fq 'Anchor.new(...) cannot store payload type `Anchor[Int32]` because it contains a nested Anchor' "$nested_anchor_payload_log"; then
  echo "nested Anchor payload build did not report the expected sema diagnostic:" >&2
  cat "$nested_anchor_payload_log" >&2
  exit 1
fi

echo "[build/fail] generic Shape specialization rejects nested Anchor without native output"
nested_anchor_shape_output="$CWD_TEST_DIR/revise_bad_anchor_nested_shape_specialization.exe"
nested_anchor_shape_log="$CWD_TEST_DIR/revise_bad_anchor_nested_shape_specialization.log"
if "$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_bad_anchor_nested_shape_specialization.cat" "$nested_anchor_shape_output" >"$nested_anchor_shape_log" 2>&1; then
  echo "nested AnchorBox specialization unexpectedly built successfully" >&2
  exit 1
fi
if [[ -e "$nested_anchor_shape_output" ]]; then
  echo "nested AnchorBox specialization rejection left a native binary behind: $nested_anchor_shape_output" >&2
  exit 1
fi
if ! grep -Fq 'AnchorBox[Anchor[Int32]]` is unsupported because an Anchor payload cannot contain another Anchor' "$nested_anchor_shape_log"; then
  echo "nested AnchorBox specialization build did not report the expected sema diagnostic:" >&2
  cat "$nested_anchor_shape_log" >&2
  exit 1
fi

echo "[build/fail] Anchor.new rejects nested Anchor inside inferred Shape payload"
nested_anchor_shape_payload_output="$CWD_TEST_DIR/revise_bad_anchor_nested_shape_payload.exe"
nested_anchor_shape_payload_log="$CWD_TEST_DIR/revise_bad_anchor_nested_shape_payload.log"
if "$CLAW_EXE" build "$FRONTEND_FIXTURE_DIR/revise_bad_anchor_nested_shape_payload.cat" "$nested_anchor_shape_payload_output" >"$nested_anchor_shape_payload_log" 2>&1; then
  echo "Anchor.new with nested Anchor inside a Shape payload unexpectedly built successfully" >&2
  exit 1
fi
if [[ -e "$nested_anchor_shape_payload_output" ]]; then
  echo "nested Anchor Shape payload rejection left a native binary behind: $nested_anchor_shape_payload_output" >&2
  exit 1
fi
if ! grep -Fq 'is unsupported because an Anchor payload cannot contain another Anchor' "$nested_anchor_shape_payload_log"; then
  echo "nested Anchor Shape payload build did not report the expected sema diagnostic:" >&2
  cat "$nested_anchor_shape_payload_log" >&2
  exit 1
fi

echo "[build/pass] valid AnchorBox[Str] and Anchor[Int32] specialization"
anchor_box_output="$CWD_TEST_DIR/revise_anchor_box_specialization.exe"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_anchor_box_specialization.cat" "$anchor_box_output" >/dev/null
"$anchor_box_output"

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

echo "[build/pass] test_native/revise_operator_groups_ab.cat"
operator_groups_output="$ARTIFACT_DIR/revise_operator_groups_ab.exe"
rm -f "$operator_groups_output" "${operator_groups_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_operator_groups_ab.cat" "$operator_groups_output" >/dev/null
expect_generated_ll "$operator_groups_output"
operator_groups_stdout="$(normalize_stdout "$operator_groups_output")"
if [[ "$operator_groups_stdout" != $'-2\n-2\n5\n4\n14\ntrue\n5\ntrue\n99\n99\ntrue\nfalse\ntrue\nfalse\ntrue' ]]; then
  echo "operator A-B native output was unexpected: $operator_groups_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_operator_short_circuit_ownership.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_operator_short_circuit_ownership.cat" "$short_circuit_ownership_output" >/dev/null
expect_generated_ll "$short_circuit_ownership_output"
short_circuit_ownership_stdout="$(normalize_stdout "$short_circuit_ownership_output")"
if [[ "$short_circuit_ownership_stdout" != "held" ]]; then
  echo "short-circuit ownership native output was unexpected: $short_circuit_ownership_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_float64_unary_negation.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_float64_unary_negation.cat" "$float_unary_output" >/dev/null
expect_generated_ll "$float_unary_output"
if ! grep -q 'fneg double' "${float_unary_output%.exe}.ll" || ! grep -q 'fneg float' "${float_unary_output%.exe}.ll"; then
  echo "Float32/Float64 unary-minus LLVM IR did not contain fneg for both types" >&2
  exit 1
fi
float_unary_stdout="$(normalize_stdout "$float_unary_output")"
if [[ "$float_unary_stdout" != $'1\n-0\n0\n-0\n0' ]]; then
  echo "Float32/Float64 unary-negation or signed-zero native output was unexpected: $float_unary_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_int32_min_literal.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_int32_min_literal.cat" "$int32_min_output" >/dev/null
expect_generated_ll "$int32_min_output"
int32_min_stdout="$(normalize_stdout "$int32_min_output")"
if [[ "$int32_min_stdout" != "-2147483648" ]]; then
  echo "Int32 minimum native output was unexpected: $int32_min_stdout" >&2
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

echo "[build/pass] test_native/revise_operator_index_compound.cat"
index_compound_output="$ARTIFACT_DIR/revise_operator_index_compound.exe"
index_compound_ll="${index_compound_output%.exe}.ll"
index_compound_failure_log="$CWD_TEST_DIR/revise_operator_index_compound-oob.log"
rm -f "$index_compound_output" "$index_compound_ll"
"$CLAW_EXE" llvm "$NATIVE_FIXTURE_DIR/revise_operator_index_compound.cat" >"$index_compound_ll"
if ! grep -q 'index.bounds_fail' "$index_compound_ll"; then
  echo "index LLVM IR did not contain the bounds-failure path" >&2
  exit 1
fi
index_call_count="$(grep -c 'call i64 @"revise_operator_index_compound::next_index"()' "$index_compound_ll" || true)"
if [[ "$index_call_count" != "1" ]]; then
  echo "compound index expression should call next_index exactly once; found $index_call_count calls" >&2
  exit 1
fi
clang -Wno-override-module \
  "$index_compound_ll" \
  "$CLAW_DIR/runtime/native_runtime.c" \
  "$NATIVE_FIXTURE_DIR/revise_operator_index_compound_harness.c" \
  -o "$index_compound_output"
index_compound_stdout="$(normalize_stdout "$index_compound_output")"
if [[ "$index_compound_stdout" != $'9\n42\n42' ]]; then
  echo "index-compound native output was unexpected: $index_compound_stdout" >&2
  exit 1
fi
ulimit -c 0
if "$index_compound_output" out-of-bounds >"$index_compound_failure_log" 2>&1; then
  echo "native index access beyond Span length unexpectedly succeeded" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_operator_str_index_read.cat"
str_index_output="$ARTIFACT_DIR/revise_operator_str_index_read.exe"
rm -f "$str_index_output" "${str_index_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_operator_str_index_read.cat" "$str_index_output" >/dev/null
expect_generated_ll "$str_index_output"
str_index_ll="${str_index_output%.exe}.ll"
if ! grep -q 'load i8' "$str_index_ll"; then
  echo "read-only Str indexing LLVM IR did not contain a byte load" >&2
  exit 1
fi
if grep -q 'store i8' "$str_index_ll"; then
  echo "read-only Str indexing LLVM IR unexpectedly contained a byte store" >&2
  exit 1
fi
str_index_stdout="$(normalize_stdout "$str_index_output")"
if [[ "$str_index_stdout" != "66" ]]; then
  echo "Str index-read native output was unexpected: $str_index_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_if_pick_values.cat"
if_pick_values_output="$ARTIFACT_DIR/revise_if_pick_values.exe"
rm -f "$if_pick_values_output" "${if_pick_values_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_if_pick_values.cat" "$if_pick_values_output" >/dev/null
expect_generated_ll "$if_pick_values_output"
if_pick_values_stdout="$(normalize_stdout "$if_pick_values_output")"
expected_if_pick_values_stdout=$'11\n22\n33\n44\n55\n77\n101\n202\n303\n404\n505\n606\n0\n-1\ninner\nouter'
if [[ "$if_pick_values_stdout" != "$expected_if_pick_values_stdout" ]]; then
  echo "if/pick value native output was unexpected: $if_pick_values_stdout" >&2
  exit 1
fi

if_pick_values_asan="$CWD_TEST_DIR/revise_if_pick_values_asan"
if printf 'int main(void) { return 0; }\n' | clang -fsanitize=address -x c - -o "$CWD_TEST_DIR/asan_probe" >/dev/null 2>&1; then
  echo "[asan/pass] if/pick branch ownership cleanup"
  clang -fsanitize=address -O1 -g "${if_pick_values_output%.exe}.ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$if_pick_values_asan"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$if_pick_values_asan" >/dev/null
else
  echo "[asan/skip] clang AddressSanitizer is unavailable"
fi

discarded_anchor_asan="$CWD_TEST_DIR/revise_discarded_anchor_temporary_asan"
if [[ -x "$CWD_TEST_DIR/asan_probe" ]]; then
  echo "[asan/pass] discarded Anchor temporary is freed and side effects execute once"
  clang -fsanitize=address -O1 -g "${discarded_anchor_output%.exe}.ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$discarded_anchor_asan"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$discarded_anchor_asan" > "$CWD_TEST_DIR/revise_discarded_anchor_temporary_asan.out"
  discarded_anchor_asan_stdout="$(tr -d '\r' < "$CWD_TEST_DIR/revise_discarded_anchor_temporary_asan.out")"
  if [[ "$discarded_anchor_asan_stdout" != "$expected_discarded_anchor_stdout" ]]; then
    echo "discarded Anchor temporary ASan output was unexpected: $discarded_anchor_asan_stdout" >&2
    exit 1
  fi
else
  echo "[asan/skip] clang AddressSanitizer is unavailable for discarded Anchor temporary test"
fi

discarded_binary_anchor_asan="$CWD_TEST_DIR/revise_discarded_binary_anchor_temporary_asan"
if [[ -x "$CWD_TEST_DIR/asan_probe" ]]; then
  echo "[asan/pass] overloaded Add Anchor result and If/Pick call results are freed once"
  clang -fsanitize=address -O1 -g "${discarded_binary_anchor_output%.exe}.ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$discarded_binary_anchor_asan"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$discarded_binary_anchor_asan" > "$CWD_TEST_DIR/revise_discarded_binary_anchor_temporary_asan.out"
  discarded_binary_anchor_asan_stdout="$(tr -d '\r' < "$CWD_TEST_DIR/revise_discarded_binary_anchor_temporary_asan.out")"
  if [[ "$discarded_binary_anchor_asan_stdout" != "$expected_discarded_binary_anchor_stdout" ]]; then
    echo "discarded BinaryExpr Anchor ASan output was unexpected: $discarded_binary_anchor_asan_stdout" >&2
    exit 1
  fi
else
  echo "[asan/skip] clang AddressSanitizer is unavailable for discarded BinaryExpr Anchor test"
fi

tuple_owned_asan="$CWD_TEST_DIR/revise_tuple_owned_destructuring_asan"
if [[ -x "$CWD_TEST_DIR/asan_probe" ]]; then
  echo "[asan/pass] owned tuple wildcard drops, field transfer, nested cleanup, and scope cleanup"
  clang -fsanitize=address -O1 -g "${tuple_owned_output%.exe}.ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$tuple_owned_asan"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$tuple_owned_asan" > "$CWD_TEST_DIR/revise_tuple_owned_destructuring_asan.out"
  tuple_owned_asan_stdout="$(tr -d '\r' < "$CWD_TEST_DIR/revise_tuple_owned_destructuring_asan.out")"
  if [[ "$tuple_owned_asan_stdout" != "$expected_tuple_owned_stdout" ]]; then
    echo "owned tuple destructuring ASan output was unexpected: $tuple_owned_asan_stdout" >&2
    exit 1
  fi
else
  echo "[asan/skip] clang AddressSanitizer is unavailable for owned tuple destructuring test"
fi

echo "[build/pass] test_native/revise_try_contract.cat"
try_contract_output="$ARTIFACT_DIR/revise_try_contract.exe"
rm -f "$try_contract_output" "${try_contract_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_try_contract.cat" "$try_contract_output" >/dev/null
expect_generated_ll "$try_contract_output"
try_contract_stdout="$(normalize_stdout "$try_contract_output")"
expected_try_contract_stdout=$'37\nstep-failed\n-1\n37\nstep-failed\n41'
if [[ "$try_contract_stdout" != "$expected_try_contract_stdout" ]]; then
  echo "try contract native output was unexpected: $try_contract_stdout" >&2
  exit 1
fi

echo "[build/pass] test_native/revise_try_owned_result.cat"
try_owned_result_output="$ARTIFACT_DIR/revise_try_owned_result.exe"
rm -f "$try_owned_result_output" "${try_owned_result_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_try_owned_result.cat" "$try_owned_result_output" >/dev/null
expect_generated_ll "$try_owned_result_output"
try_owned_result_stdout="$(normalize_stdout "$try_owned_result_output")"
expected_try_owned_result_stdout=$'temporary-ok\nno-anchor\nowned\nno-anchor\nowned\nouter\nno-anchor'
if [[ "$try_owned_result_stdout" != "$expected_try_owned_result_stdout" ]]; then
  echo "owned Result try native output was unexpected: $try_owned_result_stdout" >&2
  exit 1
fi

try_owned_result_asan="$CWD_TEST_DIR/revise_try_owned_result_asan"
if [[ -x "$CWD_TEST_DIR/asan_probe" ]]; then
  echo "[asan/pass] owned Result try success/failure ownership and leak checks"
  clang -fsanitize=address -O1 -g "${try_owned_result_output%.exe}.ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$try_owned_result_asan"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$try_owned_result_asan" > "$CWD_TEST_DIR/revise_try_owned_result_asan.out"
  try_owned_result_asan_stdout="$(tr -d '\r' < "$CWD_TEST_DIR/revise_try_owned_result_asan.out")"
  if [[ "$try_owned_result_asan_stdout" != "$expected_try_owned_result_stdout" ]]; then
    echo "owned Result try ASan output was unexpected: $try_owned_result_asan_stdout" >&2
    exit 1
  fi
else
  echo "[asan/skip] clang AddressSanitizer is unavailable"
fi

pick_anchor_payload_sanitized="$CWD_TEST_DIR/revise_pick_anchor_payload_shadowing_sanitized"
if printf 'int main(void) { return 0; }\n' | clang -fsanitize=address,undefined,leak -x c - -o "$CWD_TEST_DIR/asan_ubsan_lsan_probe" >/dev/null 2>&1; then
  echo "[asan/ubsan/lsan/pass] shadowed Anchor pick payload is read and freed exactly once"
  clang -fsanitize=address,undefined,leak -O1 -g \
    "${pick_anchor_payload_shadowing_output%.exe}.ll" \
    "$CLAW_DIR/runtime/native_runtime.c" \
    -o "$pick_anchor_payload_sanitized"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  LSAN_OPTIONS=exitcode=23 \
    "$pick_anchor_payload_sanitized" > "$CWD_TEST_DIR/revise_pick_anchor_payload_shadowing_sanitized.out"
  pick_anchor_payload_sanitized_stdout="$(tr -d '\r' < "$CWD_TEST_DIR/revise_pick_anchor_payload_shadowing_sanitized.out")"
  if [[ "$pick_anchor_payload_sanitized_stdout" != "$expected_pick_anchor_payload_shadowing_stdout" ]]; then
    echo "Anchor pick-payload sanitizer output was unexpected: $pick_anchor_payload_sanitized_stdout" >&2
    exit 1
  fi
else
  echo "[sanitizer/skip] clang ASan/UBSan/LSan runtimes are unavailable for Anchor pick-payload cleanup test"
fi

char_output="$ARTIFACT_DIR/revise_char_literals.exe"
char_ll="${char_output%.exe}.ll"
rm -f "$char_output" "$char_ll"
echo "[build/pass] test_native/revise_char_literals.cat"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_char_literals.cat" "$char_output" >/dev/null
expect_generated_ll "$char_output"
for codepoint in 233 128512 55295 57344 1114111; do
  if ! grep -Eq "i32[[:space:]]+$codepoint([^0-9]|$)" "$char_ll"; then
    echo "Char LLVM IR did not contain 32-bit Unicode scalar U+$codepoint" >&2
    exit 1
  fi
done
if "$char_output"; then
  :
else
  char_status="$?"
  echo "Char native executable failed with exit status $char_status" >&2
  exit 1
fi

char_sanitized="$CWD_TEST_DIR/revise_char_literals_sanitized"
if printf 'int main(void) { return 0; }\n' | clang -fsanitize=address,undefined,leak -x c - -o "$CWD_TEST_DIR/char_sanitizer_probe" >/dev/null 2>&1; then
  echo "[asan/ubsan/lsan/pass] native Unicode Char scalar comparisons and static global"
  clang -Wno-override-module -fsanitize=address,undefined,leak -O1 -g \
    "$char_ll" \
    "$CLAW_DIR/runtime/native_runtime.c" \
    -o "$char_sanitized"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  LSAN_OPTIONS=exitcode=23 \
    "$char_sanitized"
else
  echo "[sanitizer/skip] clang ASan/UBSan/LSan runtimes are unavailable for Unicode Char tests"
fi

echo "[build/pass] Str range-view indexing and safe get"
str_range_views_output="$ARTIFACT_DIR/revise_str_range_views.exe"
rm -f "$str_range_views_output" "${str_range_views_output%.exe}.ll"
"$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/revise_str_range_views.cat" "$str_range_views_output" >/dev/null
expect_generated_ll "$str_range_views_output"
str_range_views_stdout="$(normalize_stdout "$str_range_views_output")"
expected_str_range_views_stdout=$'A\nA\né\né\né\né\n\n\nAéB\nA\néB\n\nnone\nnone\nnone\nnone\nnone\nnone\nA'
if [[ "$str_range_views_stdout" != "$expected_str_range_views_stdout" ]]; then
  echo "Str range-view native output was unexpected:" >&2
  printf '%s\n' "$str_range_views_stdout" >&2
  exit 1
fi
str_range_views_ll="${str_range_views_output%.exe}.ll"
range_guard_line="$(grep -m1 -n 'br i1 %str.range.valid' "$str_range_views_ll" | cut -d: -f1 || true)"
range_gep_line="$(grep -m1 -n 'getelementptr inbounds i8, ptr %str.ptr' "$str_range_views_ll" | cut -d: -f1 || true)"
if [[ -z "$range_guard_line" || -z "$range_gep_line" || "$range_guard_line" -ge "$range_gep_line" ]]; then
  echo "Str range LLVM did not branch on validated bounds before its first source-byte GEP" >&2
  exit 1
fi
if ! grep -Eq 'sext i32 %str\.range\.end\.[^ ]+ to i64' "$str_range_views_ll" ||
   ! grep -Eq 'add i64 %str\.range\.end\.i64\.[0-9]+, 1' "$str_range_views_ll"; then
  echo "Str range LLVM did not widen the inclusive endpoint before incrementing it" >&2
  exit 1
fi

run_expect_str_range_defect() {
  local fixture="$1"
  local stem="${fixture%.cat}"
  local output="$ARTIFACT_DIR/${stem}.exe"
  rm -f "$output" "${output%.exe}.ll"
  "$CLAW_EXE" build "$NATIVE_FIXTURE_DIR/$fixture" "$output" >/dev/null
  expect_generated_ll "$output"
  if "$output" >/dev/null 2>&1; then
    echo "expected direct Str range defect for $fixture, but execution succeeded" >&2
    exit 1
  fi
  echo "[defect/pass] direct Str range rejects $fixture"
}

ulimit -c 0
run_expect_str_range_defect "revise_str_range_negative_defect.cat"
run_expect_str_range_defect "revise_str_range_reversed_defect.cat"
run_expect_str_range_defect "revise_str_range_oob_defect.cat"
run_expect_str_range_defect "revise_str_range_utf8_defect.cat"
run_expect_str_range_defect "revise_str_range_inclusive_overflow_defect.cat"

if [[ -x "$CWD_TEST_DIR/asan_probe" ]]; then
  str_range_views_sanitized="$CWD_TEST_DIR/revise_str_range_views_sanitized"
  echo "[sanitizer/pass] Str range-view valid/invalid-safe cases under ASan and UBSan"
  clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g \
    "$str_range_views_ll" "$CLAW_DIR/runtime/native_runtime.c" -o "$str_range_views_sanitized"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$str_range_views_sanitized" > "$CWD_TEST_DIR/revise_str_range_views_sanitized.out"
  sanitized_str_range_views_stdout="$(tr -d '\r' < "$CWD_TEST_DIR/revise_str_range_views_sanitized.out")"
  if [[ "$sanitized_str_range_views_stdout" != "$expected_str_range_views_stdout" ]]; then
    echo "sanitized Str range-view output was unexpected" >&2
    exit 1
  fi
else
  echo "[sanitizer/skip] clang ASan/UBSan runtimes are unavailable for Str range-view test"
fi
