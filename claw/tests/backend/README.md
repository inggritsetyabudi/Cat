# Revised Backend Tests

This suite is intentionally small and only covers revised-language backend lowering that is already supported today.

Recommended runner (from `claw/`):
- `bash tests/backend/run_backend_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash tests/backend/run_backend_tests.sh`

Artifacts:
- Generated LLVM IR is left in `artifacts/` so it can be inspected after the runner finishes.

Fixtures live in `fixtures/`, except `revise_maybe.cat`, which is shared from `tests/frontend/fixtures/`.

Current fixtures:
- `revise_result_llvm.cat`
  Exercises revised `Result[T, E]`, `Ok`, `Fail`, `try`, `try ... else`, integer printing, and string printing through the LLVM backend.
- `revise_maybe.cat`
  Verifies revised `Maybe[T]` lowering can still produce valid LLVM IR.

The backend runner checks textual LLVM IR and validates it with `llvm-as`. Native executable coverage lives in `tests/native/`.
