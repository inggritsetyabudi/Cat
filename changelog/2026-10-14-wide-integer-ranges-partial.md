# Integer-width `Range` support (2026-10-14)

This review slice extends PRD §7 `Range` construction, `in` membership, ordinary scans, and indexed scans from the existing `Int8`, `Int16`, `Int32`, `UInt8`, and `Char` set to also support `Int64`, `UInt16`, `UInt32`, `UInt64`, and `USize`. PRD §7 remains partial: `Int128` and `UInt128` remain outside this change, and neither the PRD nor milestone status is marked complete. `Str` slicing remains restricted to `Range[Int32]`.

## Behavior and implementation

- Bounds must still have the same scalar type; membership must still pair a scalar with `Range` of that exact type. Equal LLVM widths do not make `UInt64` and `USize` interchangeable.
- LLVM lowering now lays out each supported `Range[T]` from its scalar LLVM type. On the x86-64 target used for validation, `Int64`, `UInt64`, and `USize` use `i64` elements; `UInt16`/`UInt32` preserve `i16`/`i32` elements.
- Membership and scans use signed comparisons for signed element types and unsigned comparisons for unsigned element types. Narrow scan increments retain their element width, and inclusive maximum endpoints are recognized before advancing the iterator.
- OIR scan lowering and LLVM iterator recognition accept the supported `Range[T]` family without removing `Char` or the previously supported widths.
- Scalar/runtime APIs, `Range[Int128]`/`Range[UInt128]`, and `Str` slicing behavior are unchanged.

## Regression coverage

- The native fixture exercises all five added types, typed bounds and annotations, matching membership, ordinary and indexed scans, unsigned values across the sign bit, and inclusive maximum endpoints for each width.
- Frontend/OIR negative fixtures preserve mixed-bound and mixed-membership diagnostics, reject `Int128`/`UInt128`, ensure `UInt64` cannot use `Range[USize]`, and keep `Str` slicing with `Range[Int64]` rejected.
- OIR golden and LLVM backend assertions cover `i16`/`i32`/`i64` aggregate layouts, comparison signedness, and width-specific scan increments.

## Verification

- Focused frontend and OIR suites passed.
- Full `bash claw/tests/run_all_tests.sh` passed with exit code 0, including PRD doc-tests, backend LLVM, native execution, and CLI status tests.
- The new native fixture also passed ASan/UBSan/LSan with leak detection and halt-on-error enabled; sanitized stdout matched the regular run exactly (26 lines).
- Generated test-runner artifacts were restored/removed after verification; no unrelated artifact diffs are included.
