# Modularize numeric literal analysis

- Extract numeric-literal parsing, target-fit checks, inference helpers, and literal classification from `sema.cpp` into the private `analysis/numeric_literals` module.
- Keep `SemanticAnalyzer` state, expression traversal, diagnostic wording, and the order of semantic checks in `sema.cpp` unchanged.
- Add an exact AIR CLI golden, a UInt128 overflow diagnostic regression, and a native Cat fixture covering typed integer literals.
- This is a structural refactor only; no PRD status, Arena implementation, operator/slice worktree, or prior workspace/OIR changes were changed.

## Validation

- Baseline compiler build and full test suites passed before extraction.
- Post-extraction `cmake --build claw/build --target claw -j2` passed.
- `bash claw/tests/run_all_tests.sh` passed, including the exact numeric AIR golden, overflow diagnostic check, and native fixture (expected output `127`).
- `git diff --check` and `bash -n` for the modified frontend/native runners passed.
- Independent read-only code review found no correctness issues; branch remains uncommitted and unpushed.
