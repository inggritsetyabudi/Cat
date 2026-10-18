# Target-direct Float32/Float64 literal consistency

This is one bounded literal-correctness workstream based on main `e7789c6efedff677b81c2f883fe8498e32c30554`. It does not complete v0.4 or create a release.

## User-visible fixes

- Zero significands such as `0.0e1`, `0e9999`, and `0e-9999` are valid; exponent digits no longer make mathematical zero appear nonzero.
- Semantic validation and LLVM materialization use the same target-directed `std::from_chars` conversion to binary32/binary64. This removes host `long double` narrowing and its boundary-dependent double rounding. Finite subnormals remain valid; overflow and nonzero values rounded to zero remain compile-time errors.
- Symbolic literal types remain available during contextual inference. A final semantic pass resolves remaining float-literal expression types to default `Float64` and validates every analyzed float leaf before lowering, including unary operands, discarded values, tuple members, branch tails, and carrier payloads. Diagnostics are ordered by source position.
- Static Float32/Float64 constants use the same materializer as local constants instead of the old integer `0` fallback. Integer/radix literals in an existing float context keep their established exactness restrictions.
- LLVM's textual Float32 hex encoding is obtained by exact promotion of the already-rounded binary32 bits to binary64; it is not a second decimal conversion. Unary minus remains `fneg`, preserving signed zero.

## Actual baseline reproduction before source edits

A clean Release compiler from main was used for Cat `check`, `build`, and executable runs, not just source inspection:

- `0.0e1_Float64`, `0e-9999_Float32`, and `0e9999_Float64` were incorrectly rejected by both check and build.
- Normal Float32/Float64 subnormals and `1.25_Float32` checked, built, and ran successfully, establishing behavior to preserve.
- `340282356779733661637539395458142568447.0_Float32`, exactly below the binary32 overflow rounding threshold, was wrongly rejected after host `long double` rounding. The exact threshold itself must still be rejected.
- Inferred `1e9999`/`1e-9999`, discarded overflow, tuple payloads, and uncontextualized branch tails could pass check and fail only during LLVM lowering.
- A follow-up comparison with an immutable main build showed an accepted static Float32 initializer emitted invalid LLVM `constant float 0`. The same shared conversion now handles this already-accepted syntax.

## Regression evidence

`claw/tests/native/run_float_target_direct_tests.py` is part of `run_all_tests.sh`. Two positive native fixtures cover typed/suffixed/inferred literals, ASCII case variants and aliases, separators and exponents, zero exponents, direct-rounding halfway boundaries, maximum finite/minimum normal/subnormal values, unary minus, calls/returns, static globals, tuple destructuring, Anchor/Maybe payloads, assignment, and existing contextual if/pick behavior. Binding-specific LLVM assertions check target bits, not a new output-formatting contract. Each negative case is compiled independently through both check and build, requires the intended semantic diagnostic, and must leave no LLVM/executable artifact. Generated native programs also run under ASan/UBSan/LSan when the toolchain can link those sanitizers.

Final local gates passed on separate disposable source snapshots: a clean Clang Release build and complete `bash claw/tests/run_all_tests.sh`; then a clean Release compiler built with `-fsanitize=address,undefined -fno-omit-frame-pointer` and the same complete suite with leak detection and halt-on-error enabled. Both runs ended with `All C@ test suites passed successfully!`; the target-directed suite passed all **47 independent rejection cases** through both check and build, plus both generated programs under ASan/UBSan/LSan. An independent source review found no blockers and also built a separate GCC Debug copy and ran the full target-directed regression successfully. Local success alone is not Linux/Windows CI success; PR readiness remains contingent on both CI jobs.

## Fast / Safe / Simple

- **Fast:** changes operate at compile time. No new runtime allocation, dispatch, float formatting, or checks are introduced.
- **Safe:** supported literals are validated before codegen, and the same conversion determines emitted bits. Overflow/underflow rejection has independent negative tests, including negative unary and inferred forms.
- **Simple:** one shared target conversion serves semantic analysis, local LLVM constants, and static globals. One post-inference finalization pass covers otherwise-uncontextualized literal leaves instead of scattered per-container checks.

## Explicit limits

Float128 remains an unimplemented v0.4 item requiring its specified APFloat/fp128 workstream. F-string, Show/formatting semantics, Arena, new cross-width implicit conversions, new cross-branch inference, broader fixed-array element support, and unrelated ownership/arithmetic work are not added. Existing mixed Float32/Float64 rejection behavior is retained. PR #22 and PR #23, including their branches, are left untouched. No version bump, merge, tag, or release is part of this change.
