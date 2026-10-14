# `Range[Int128]` and `Range[UInt128]` support (2026-10-14)

This follow-up extends the partial PRD §7 Range implementation to the existing signed and unsigned 128-bit integer scalar types. It does not change canonical integer-literal parsing, overflow checking, scalar arithmetic, or runtime printing.

## Behavior and implementation

- Construction and type annotations accept `Range[Int128]` and `Range[UInt128]`. Bounds must still have the exact same scalar type; membership accepts only a matching scalar/`Range[T]` pair.
- LLVM uses the existing `i128` scalar representation and selects signed comparisons for `Int128` and unsigned comparisons for `UInt128` in membership and scans.
- Ordinary and indexed scans retain the range element type and `USize` index. Inclusive termination checks the endpoint before incrementing, so a scan ending at the signed or unsigned maximum does not wrap.
- Stored `Range[i128]` values use the natural 16-byte alignment. On the x86-64 validation target, `{ i128, i128, i1 }` has size 48 and alignment 16; nested tuples retain that alignment and padding.
- `Str` slicing remains restricted to `Range[Int32]`; no 128-bit runtime-printing support is implied. PRD §7 remains partial.

## Regression coverage

The native fixture checks signed minimum and maximum, unsigned zero and maximum, values across the unsigned sign bit, singleton/exclusive/inclusive membership, short ordinary and indexed scans ending at the maxima, indirect function parameters, nested tuple storage, and 16-byte alignment. Frontend/OIR tests reject mixed `Int128`/`UInt128` bounds and membership and continue to reject `Range[Int128]` string slicing. Existing Range, Array, tuple, unsigned-print, and `Range[Int32]` slicing tests remain in place.

## Verification

- Fresh CMake configure and build with Clang 18.1.3 in a disposable copy passed.
- `bash claw/tests/run_all_tests.sh` passed in that copy, including frontend, OIR, LLVM backend, native, ASan/UBSan/LSan, documentation, and CLI-status suites.
- The 36-line native boundary/membership/scan output matched exactly under the regular run and ASan/UBSan/LSan.
