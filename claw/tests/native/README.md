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

## Native path regression

Run `python3 tests/native/run_native_path_tests.py` from `claw/` (or invoke it by
absolute path). The runner uses `CLAW_EXE` when set, otherwise the standard
`build/` and `build-ucrt64-clang/` candidates. It intentionally invokes the
compiler with `subprocess` argument vectors and an unrelated working directory,
then verifies:

- a `.cat` input with spaces, output directories and a literal `& marker; $ value.exe` filename;
- a copied compiler and `runtime/native_runtime.c` under `compiler bundle with spaces/runtime/`;
- both `build` and `build-native` with no explicit output target;
- a no-space control case.

Every case requires exact `native-path-ok\n` output, exit status `7`, and a
generated `.ll` beside the executable. Permanent results remain under
`artifacts/native path regression/`; stale `.exe` and `.ll` files for each
target are removed before rebuilding.

`python3 tests/native/run_windows_argv_tests.py` compiles an argument probe into
`artifacts/argv probe with spaces/`. It checks 12 exact CRT serialization cases
on every platform. On Windows it also invokes `_wspawnvp` and checks that the
child receives every argument unchanged, including empty strings, tabs, quotes,
and trailing backslashes. Only this Windows process round-trip is explicitly
skipped on other platforms. Both new runners are part of `run_all_tests.sh`.
