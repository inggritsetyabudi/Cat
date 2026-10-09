# Revised Native Tests

This suite covers revised-language native executable generation for the currently supported subset.

Recommended runner (from `claw/`):
- `bash tests/native/run_native_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash tests/native/run_native_tests.sh`

Artifacts:
- Built executables and generated LLVM IR are left in `artifacts/` so both `.exe` and `.ll` outputs can be inspected after the runner finishes.
- A regression build runs from a temporary working directory and verifies the compiler can still locate its bundled native runtime.

Native-only fixtures live in `fixtures/`; cross-suite frontend fixtures are listed with their paths below.

Current fixtures:
- `revise_single_file.cat`
  A direct single-file build that proves revised surface syntax can build and run as a native executable.
- `revise_workspace/`
  A workspace build with `claw.toml` and root `main.cat`, proving revised project loading also reaches native execution.
- `revise_maybe.cat`
  Confirms the current revised `Maybe[T]` subset still builds and runs natively.
- `revise_exit_code.cat`
  Verifies `fn main() -> Int32` returns the expected OS exit code.
- `../frontend/fixtures/revise_nested_scoped_generic.cat`
  Builds and executes `Box[Name[s]]` through generic construction, indirect parameter/return ABI, calls, and field access.
- `../frontend/fixtures/revise_nested_scoped_workspace/`
  Builds and executes shared generic shapes and nested scoped signatures imported across modules.

The suite now includes the supported imported-workspace path and keeps its generated LLVM and executables in `artifacts/`.
