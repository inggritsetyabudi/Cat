# Modularize OIR formatting

- Move OIR rendering helpers and `formatOirRealm`, `formatOirProgram`, and `emitOirProgram` implementations from `oir.cpp` into `oir_format.cpp`; keep their declarations in `oir.h` unchanged.
- Keep OIR lowering, semantics, diagnostics, and public API behavior unchanged; register the new translation unit with CMake.
- Add an OIR golden-test suite that runs five real Cat fixtures (including a multi-module workspace) and checks emitted output byte-for-byte against snapshots captured before extraction.
- This is a structural refactor only; no Arena implementation or behavior was changed.

## Validation

- Baseline build and all existing test suites passed before extraction.
- Post-refactor compiler build and all test suites passed, including the new OIR golden suite and native tests.
- All 35 generated backend/native LLVM IR artifacts passed both `llvm-as` and `clang` verification/compilation.
- `git diff --check` passed.
