# Modularize built-in method specifications

- Move the declarative signatures for ordinary built-in methods out of `sema.cpp` into the private `analysis/builtin_methods` module.
- Keep method lookup precedence, receiver checking, diagnostics, and all special-case resolution paths in `sema.cpp` unchanged; the metadata entries and construction order are unchanged.
- Add Cat regressions for receiver-derived `slice` views, mutable `Vec.clear()`, and rejection of an immutable receiver.
- This is a structural refactor only; no PRD status, Arena implementation, operator/slice worktree, or prior workspace/OIR changes were changed.

## Validation

- Baseline compiler build and full test suites passed before extraction.
- Post-extraction `cmake --build claw/build --target claw -j 4` passed.
- `bash claw/tests/run_all_tests.sh` passed, including the new built-in view and receiver-diagnostic regressions; all frontend, OIR golden, backend LLVM, and native suites passed.
- `git diff --check` and `bash -n` for the modified frontend/native runners passed.
- Independent read-only review confirmed the extracted metadata table and resolver integration preserve behavior; no blocker was found. Branch remains uncommitted and unpushed.
