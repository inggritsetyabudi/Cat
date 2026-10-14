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
implements_ll="$ARTIFACT_DIR/revise_implements.ll"
if_pick_values_ll="$ARTIFACT_DIR/revise_if_pick_values.ll"
try_contract_ll="$ARTIFACT_DIR/revise_try_contract.ll"
int8_range_ll="$ARTIFACT_DIR/revise_int8_range.ll"
int16_range_ll="$ARTIFACT_DIR/revise_int16_range.ll"
rm -f "$result_ll" "$maybe_ll" "$scope_ll" "$anchor_ll" "$anchor_choice_ll" "$view_shape_ll" "$scoped_type_ll" "$nested_scoped_ll" "$nested_scoped_workspace_ll" "$str_search_ll" "$implements_ll" "$if_pick_values_ll"
rm -f "$try_contract_ll" "$int8_range_ll" "$int16_range_ll"

run_llvm "test_backend/revise_result_llvm.cat" "$BACKEND_FIXTURE_DIR/revise_result_llvm.cat" "$result_ll"
llvm_output="$(tr -d '\r' < "$result_ll")"
if [[ "$llvm_output" != *'define internal void @"revise_result_llvm::step"'* ]] ||
   [[ "$llvm_output" != *'define internal void @"revise_result_llvm::show"'* ]] ||
   [[ "$llvm_output" != *'@"claw.runtime.println.i32"'* ]] ||
   [[ "$llvm_output" != *'@"claw.runtime.println.slice"'* ]] ||
   [[ "$llvm_output" != *'entry.try_ok.0:'* ]] ||
   [[ "$llvm_output" != *'try_fail_0:'* ]]; then
  echo "revised backend LLVM output did not include the expected Result/try lowering markers" >&2
  exit 1
fi
if [[ "$llvm_output" == *'%lift.'* || "$llvm_output" == *'.lift_'* ]]; then
  echo "revised backend LLVM output still contains legacy lift markers" >&2
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

run_llvm "test_native/revise_implements.cat" "$SCRIPT_DIR/../native/fixtures/revise_implements.cat" "$implements_ll"

run_llvm "test_native/revise_if_pick_values.cat" "$SCRIPT_DIR/../native/fixtures/revise_if_pick_values.cat" "$if_pick_values_ll"
if_pick_values_output="$(tr -d '\r' < "$if_pick_values_ll")"
if [[ "$if_pick_values_output" != *'br i1'* ]] ||
   [[ "$if_pick_values_output" != *'switch i32'* ]] ||
   [[ "$if_pick_values_output" != *'label %pick___'* ]] ||
   [[ "$if_pick_values_output" != *'store i32'* ]] ||
   [[ "$if_pick_values_output" != *'load i32'* ]] ||
   [[ "$if_pick_values_output" != *'call void @"claw.runtime.anchor.free"'* ]]; then
  echo "if/pick value LLVM output did not contain branch/result storage and choice switch markers" >&2
  exit 1
fi

run_llvm "test_native/revise_try_contract.cat" "$SCRIPT_DIR/../native/fixtures/revise_try_contract.cat" "$try_contract_ll"
try_unwrap_one_llvm="$(sed -n '/define internal void @"revise_try_contract::unwrap_one"/,/^}/p' "$try_contract_ll")"
try_tag_check_count="$(printf '%s\n' "$try_unwrap_one_llvm" | grep -c ' = icmp eq i32 .*try.tag' || true)"
if [[ "$try_unwrap_one_llvm" != *'call void @"revise_try_contract::nested"('* ]] ||
   [[ "$try_unwrap_one_llvm" != *'entry.try_ok.0:'* ]] ||
   [[ "$try_tag_check_count" != "1" ]]; then
  echo "nested Result LLVM lowering did not check exactly the outer Result tag" >&2
  exit 1
fi

range_ll="$ARTIFACT_DIR/revise_int32_range.ll"
rm -f "$range_ll"
run_llvm "test_native/revise_int32_range.cat" "$SCRIPT_DIR/../native/fixtures/revise_int32_range.cat" "$range_ll"
range_output="$(tr -d '\r' < "$range_ll")"
if [[ "$range_output" != *'define internal i32 @"revise_int32_range::contains"(i32 %arg.value, { i32, i32, i1 } %arg.range)'* ]] ||
   [[ "$range_output" != *'insertvalue { i32, i32, i1 } zeroinitializer'* ]] ||
   [[ "$range_output" != *'extractvalue { i32, i32, i1 }'* ]] ||
   [[ "$range_output" != *'icmp sge i32'* ]] ||
   [[ "$range_output" != *'icmp slt i32'* ]] ||
   [[ "$range_output" != *'icmp sle i32'* ]] ||
   [[ "$range_output" != *'select i1'* ]]; then
  echo "Int32 range LLVM output did not contain the direct aggregate ABI and expected bound tests" >&2
  exit 1
fi
uint8_range_ll="$ARTIFACT_DIR/revise_uint8_range.ll"
rm -f "$uint8_range_ll"
run_llvm "test_native/revise_uint8_range.cat" "$SCRIPT_DIR/../native/fixtures/revise_uint8_range.cat" "$uint8_range_ll"
uint8_range_output="$(tr -d '\r' < "$uint8_range_ll")"
if [[ "$uint8_range_output" != *'define internal i32 @"revise_uint8_range::contains"(i8 %arg.value, { i8, i8, i1 } %arg.range)'* ]] ||
   [[ "$uint8_range_output" != *'icmp uge i8'* ]] ||
   [[ "$uint8_range_output" != *'icmp ult i8'* ]] ||
   [[ "$uint8_range_output" != *'icmp ule i8'* ]] ||
   [[ "$uint8_range_output" != *'add i8'* ]] ||
   [[ "$uint8_range_output" != *'icmp eq i8'* ]] ||
   [[ "$uint8_range_output" != *'declare void @"claw.runtime.println.u8"(i8 zeroext)'* ]] ||
   [[ "$uint8_range_output" != *'call void @"claw.runtime.println.u8"(i8 zeroext '* ]]; then
  echo "UInt8 range LLVM output did not contain its i8 ABI, unsigned bounds, narrow increment, and unsigned printer" >&2
  exit 1
fi
run_llvm "test_native/revise_int8_range.cat" "$SCRIPT_DIR/../native/fixtures/revise_int8_range.cat" "$int8_range_ll"
int8_range_output="$(tr -d '\r' < "$int8_range_ll")"
if [[ "$int8_range_output" != *'define internal i32 @"revise_int8_range::contains"(i8 %arg.value, { i8, i8, i1 } %arg.range)'* ]] ||
   [[ "$int8_range_output" != *'icmp sge i8'* ]] ||
   [[ "$int8_range_output" != *'icmp slt i8'* ]] ||
   [[ "$int8_range_output" != *'icmp sle i8'* ]] ||
   [[ "$int8_range_output" != *'icmp eq i8'* ]] ||
   [[ "$int8_range_output" != *'add i8'* ]] ||
   [[ "$int8_range_output" != *'add i64'* ]] ||
   [[ "$int8_range_output" == *'icmp uge i8'* || "$int8_range_output" == *'icmp ult i8'* || "$int8_range_output" == *'icmp ule i8'* ]]; then
  echo "Int8 range LLVM output did not preserve signed i8 bounds, narrow terminal checks, or USize scan indexes" >&2
  exit 1
fi
run_llvm "test_native/revise_int16_range.cat" "$SCRIPT_DIR/../native/fixtures/revise_int16_range.cat" "$int16_range_ll"
int16_range_output="$(tr -d '\r' < "$int16_range_ll")"
if [[ "$int16_range_output" != *'define internal i32 @"revise_int16_range::contains"(i16 %arg.value, { i16, i16, i1 } %arg.range)'* ]] ||
   [[ "$int16_range_output" != *'icmp sge i16'* ]] ||
   [[ "$int16_range_output" != *'icmp slt i16'* ]] ||
   [[ "$int16_range_output" != *'icmp sle i16'* ]] ||
   [[ "$int16_range_output" != *'icmp eq i16'* ]] ||
   [[ "$int16_range_output" != *'add i16'* ]] ||
   [[ "$int16_range_output" != *'add i64'* ]] ||
   [[ "$int16_range_output" == *'icmp uge i16'* || "$int16_range_output" == *'icmp ult i16'* || "$int16_range_output" == *'icmp ule i16'* ]]; then
  echo "Int16 range LLVM output did not preserve its aggregate ABI, signed comparisons, terminal check, or USize scan indexes" >&2
  exit 1
fi
int8_print_ll="$ARTIFACT_DIR/revise_int8_print.ll"
rm -f "$int8_print_ll"
run_llvm "test_native/revise_int8_print.cat" "$SCRIPT_DIR/../native/fixtures/revise_int8_print.cat" "$int8_print_ll"
int8_print_output="$(tr -d '\r' < "$int8_print_ll")"
if [[ "$int8_print_output" != *'declare void @"claw.runtime.print.i8"(i8 signext)'* ]] ||
   [[ "$int8_print_output" != *'call void @"claw.runtime.print.i8"(i8 signext '* ]] ||
   [[ "$int8_print_output" != *'declare void @"claw.runtime.println.i8"(i8 signext)'* ]] ||
   [[ "$int8_print_output" != *'call void @"claw.runtime.println.i8"(i8 signext '* ]] ||
   [[ "$int8_print_output" == *'i8 zeroext'* ]]; then
  echo "Int8 runtime printer LLVM output did not use signed extension consistently at declarations and callsites" >&2
  exit 1
fi
int16_print_ll="$ARTIFACT_DIR/revise_int16_print.ll"
rm -f "$int16_print_ll"
run_llvm "test_native/revise_int16_print.cat" "$SCRIPT_DIR/../native/fixtures/revise_int16_print.cat" "$int16_print_ll"
int16_print_output="$(tr -d '\r' < "$int16_print_ll")"
if [[ "$int16_print_output" != *'declare void @"claw.runtime.println.i16"(i16 signext)'* ]] ||
   [[ "$int16_print_output" != *'call void @"claw.runtime.println.i16"(i16 signext '* ]] ||
   [[ "$int16_print_output" == *'i16 zeroext'* ]]; then
  echo "Int16 runtime printer LLVM output did not use signed extension consistently at declarations and callsites" >&2
  exit 1
fi
uint8_print_ll="$ARTIFACT_DIR/revise_uint8_print.ll"
rm -f "$uint8_print_ll"
run_llvm "test_native/revise_uint8_print.cat" "$SCRIPT_DIR/../native/fixtures/revise_uint8_print.cat" "$uint8_print_ll"
uint8_print_output="$(tr -d '\r' < "$uint8_print_ll")"
if [[ "$uint8_print_output" != *'declare void @"claw.runtime.print.u8"(i8 zeroext)'* ]] ||
   [[ "$uint8_print_output" != *'call void @"claw.runtime.print.u8"(i8 zeroext '* ]]; then
  echo "UInt8 print LLVM output did not apply zeroext to its declaration and callsite" >&2
  exit 1
fi
char_range_ll="$ARTIFACT_DIR/revise_char_range.ll"
rm -f "$char_range_ll"
run_llvm "test_native/revise_char_range.cat" "$SCRIPT_DIR/../native/fixtures/revise_char_range.cat" "$char_range_ll"
char_range_output="$(tr -d '\r' < "$char_range_ll")"
if [[ "$char_range_output" != *"{ i32, i32, i1 }"* ]] ||
   [[ "$char_range_output" != *"55295"* ]] ||
   [[ "$char_range_output" != *"57344"* ]]; then
  echo "Char range LLVM output omitted its 32-bit range layout or surrogate-skip successor" >&2
  exit 1
fi

static_ll="$ARTIFACT_DIR/revise_static.ll"
rm -f "$static_ll"
run_llvm "test_native/revise_static.cat" "$SCRIPT_DIR/../native/fixtures/revise_static.cat" "$static_ll"

contract_ll="$ARTIFACT_DIR/revise_contract.ll"
rm -f "$contract_ll"
run_llvm "test_native/revise_contract.cat" "$SCRIPT_DIR/../native/fixtures/revise_contract.cat" "$contract_ll"

arena_ll="$ARTIFACT_DIR/revise_arena.ll"
rm -f "$arena_ll"
run_llvm "test_native/revise_arena.cat" "$SCRIPT_DIR/../native/fixtures/revise_arena.cat" "$arena_ll"

op_ll="$ARTIFACT_DIR/revise_operator_overload.ll"
rm -f "$op_ll"
run_llvm "test_native/revise_operator_overload.cat" "$SCRIPT_DIR/../native/fixtures/revise_operator_overload.cat" "$op_ll"

fixed_array_ll="$ARTIFACT_DIR/revise_fixed_array.ll"
rm -f "$fixed_array_ll"
run_llvm "test_native/revise_fixed_array.cat" "$SCRIPT_DIR/../native/fixtures/revise_fixed_array.cat" "$fixed_array_ll"
fixed_array_output="$(tr -d '\r' < "$fixed_array_ll")"
fixed_array_main="$(sed -n '/define internal i32 @"revise_fixed_array::main"/,/^}/p' "$fixed_array_ll")"
array_index_guard_line="$(printf '%s\n' "$fixed_array_main" | grep -m1 -nE 'array\.index\.in_bounds.* = icmp ult i64' | cut -d: -f1 || true)"
array_index_gep_line="$(printf '%s\n' "$fixed_array_main" | grep -m1 -nE 'array\.index\.element_ptr.* = getelementptr inbounds \[3 x i32\]' | cut -d: -f1 || true)"
array_scan_guard_line="$(printf '%s\n' "$fixed_array_main" | grep -m1 -nE 'array\.scan\.has_item.* = icmp ult i64' | cut -d: -f1 || true)"
array_scan_gep_line="$(printf '%s\n' "$fixed_array_main" | grep -m1 -nE 'array\.iter\.item\.ptr.* = getelementptr inbounds \[3 x i32\]' | cut -d: -f1 || true)"
array_scan_header_line="$(printf '%s\n' "$fixed_array_main" | grep -m1 -n '^scan_header_6:' | cut -d: -f1 || true)"
array_factory_call_count="$(printf '%s\n' "$fixed_array_main" | grep -c 'call \[3 x i32\] @"revise_fixed_array::make_array"()' || true)"
array_last_factory_call_line="$(printf '%s\n' "$fixed_array_main" | grep -n 'call \[3 x i32\] @"revise_fixed_array::make_array"()' | tail -1 | cut -d: -f1 || true)"
if [[ "$fixed_array_output" != *'define internal [3 x i32] @"revise_fixed_array::make_array"()'* ]] ||
   [[ "$fixed_array_output" != *'define internal i32 @"revise_fixed_array::select"([3 x i32] %arg.array, i64 %arg.index)'* ]] ||
   [[ "$fixed_array_output" != *'call void @llvm.trap()'* ]] ||
   [[ -z "$array_index_guard_line" || -z "$array_index_gep_line" || "$array_index_guard_line" -ge "$array_index_gep_line" ]] ||
   [[ -z "$array_scan_guard_line" || -z "$array_scan_gep_line" || "$array_scan_guard_line" -ge "$array_scan_gep_line" ]] ||
   [[ "$array_factory_call_count" != "2" || -z "$array_scan_header_line" || "$array_last_factory_call_line" -ge "$array_scan_header_line" ]]; then
  echo "fixed-array LLVM output did not preserve [N x i32] ABI, evaluate scan rvalue before the loop, or check bounds before GEP/read" >&2
  exit 1
fi

fixed_array_formats_ll="$ARTIFACT_DIR/revise_fixed_array_length_formats.ll"
rm -f "$fixed_array_formats_ll"
run_llvm "test_frontend/revise_fixed_array_length_formats.cat" \
  "$FRONTEND_FIXTURE_DIR/revise_fixed_array_length_formats.cat" "$fixed_array_formats_ll"
fixed_array_formats_output="$(tr -d '\r' < "$fixed_array_formats_ll")"
if [[ "$fixed_array_formats_output" != *'getelementptr inbounds [3 x i32]'* ]] ||
   [[ "$fixed_array_formats_output" != *'define internal void @"revise_fixed_array_length_formats::accepts_decimal_separator"(ptr %arg.values.addr)'* ]] ||
   [[ "$fixed_array_formats_output" != *'define internal void @"revise_fixed_array_length_formats::accepts_hex"(ptr %arg.values.addr)'* ]] ||
   [[ "$fixed_array_formats_output" != *'define internal void @"revise_fixed_array_length_formats::accepts_binary"(ptr %arg.values.addr)'* ]] ||
   [[ "$fixed_array_formats_output" != *'define internal void @"revise_fixed_array_length_formats::accepts_octal"(ptr %arg.values.addr)'* ]]; then
  echo "fixed-array radix/separator lengths did not produce their expected LLVM aggregate layouts" >&2
  exit 1
fi

fixed_array_overflow_output=""
if fixed_array_overflow_output="$("$CLAW_EXE" llvm "$FRONTEND_FIXTURE_DIR/revise_bad_fixed_array_size_overflow.cat" 2>&1)"; then
  echo "LLVM lowering unexpectedly accepted a fixed array whose byte size overflows the target limit" >&2
  exit 1
fi
if [[ "$fixed_array_overflow_output" != *"Fixed Array byte size overflows the target size limit"* ]] ||
   [[ "$fixed_array_overflow_output" == *'[4611686018427387904 x i32]'* ]]; then
  echo "overflowing fixed array did not fail before emitting its LLVM aggregate" >&2
  echo "$fixed_array_overflow_output" >&2
  exit 1
fi

fixed_array_large_ll="$ARTIFACT_DIR/revise_fixed_array_large.ll"
rm -f "$fixed_array_large_ll"
run_llvm "test_native/revise_fixed_array_large.cat" "$SCRIPT_DIR/../native/fixtures/revise_fixed_array_large.cat" "$fixed_array_large_ll"
fixed_array_large_output="$(tr -d '\r' < "$fixed_array_large_ll")"
if [[ "$fixed_array_large_output" != *'define internal void @"revise_fixed_array_large::make_large"(ptr %ret.slot)'* ]] ||
   [[ "$fixed_array_large_output" != *'define internal i32 @"revise_fixed_array_large::last_value"(ptr %arg.values.addr)'* ]] ||
   [[ "$fixed_array_large_output" != *'getelementptr inbounds [5 x i32]'* ]]; then
  echo "Array[Int32,5] LLVM output did not use the fixed layout and indirect aggregate ABI" >&2
  exit 1
fi
