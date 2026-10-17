# Testing C@

This guide documents the test workflow for the Claw compiler. The authoritative CI workflow is [`.github/workflows/ci.yml`](../.github/workflows/ci.yml); this page makes the same build, environment, suite order, and artifact behavior usable locally.

See the [build guide](./BUILDING.md) for broader build notes, the [CLI reference](./CLI.md) for compiler commands, and [implementation status](./STATUS.md) for the supported subset. The complete runner is [`claw/tests/run_all_tests.sh`](../claw/tests/run_all_tests.sh).

## Test prerequisites

- Linux: Clang, CMake, Ninja, Bash, Python 3.
- Windows: MSYS2 UCRT64, UCRT64 Clang/CMake/Ninja, `diffutils`, Python, and Bash.
- A built `claw` executable.
- A working tree whose fixture files have not been rewritten with incompatible line endings.

CMake requires version 3.20 or newer. The CI jobs use Release builds and a clean `claw/build-ci` directory. Tests are shell-driven and should be invoked from the repository root.

## CI-equivalent builds

### Linux (Ubuntu 24.04)

```bash
sudo apt-get update
sudo apt-get install --yes --no-install-recommends clang cmake ninja-build
rm -rf claw/build-ci
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4
export CLAW_EXE="$PWD/claw/build-ci/claw"
```

Verify the tools when diagnosing a CI-only issue:

```bash
clang++ --version
cmake --version
ninja --version
```

### Windows (MSYS2 UCRT64)

Open an MSYS2 UCRT64 shell. The CI installation is:

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-clang \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  diffutils \
  python
```

Configure and build from the repository root:

```bash
git config --local core.autocrlf input
rm -rf claw/build-ci
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4
export CLAW_EXE="$(cygpath -u "$PWD")/claw/build-ci/claw.exe"
printf 'Using compiler: %s\n' "$CLAW_EXE"
```

The CI workflow configures checkout in PowerShell and runs all subsequent commands with the MSYS2 shell. The local Git setting above avoids a global change; it does not rewrite an existing checkout's line endings. If PowerShell is the starting point, open UCRT64 explicitly rather than relying on a generic `bash.exe`, then run the shell commands above:

```powershell
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64
```

Useful version output is:

```bash
clang++ --version
cmake --version
ninja --version
diff --version
python3 --version
```

## Complete suite

Run all suites in CI order:

```bash
bash claw/tests/run_all_tests.sh
```

The runner executes:

1. PRD documentation tests: `bash claw/tests/docs/run_doc_tests.sh`
2. Documentation command tests: `python3 claw/tests/docs/run_doc_command_tests.py`
3. Frontend tests: `bash claw/tests/frontend/run_frontend_tests.sh`
4. OIR formatter golden tests: `bash claw/tests/oir/run_oir_tests.sh`
5. Backend LLVM tests: `bash claw/tests/backend/run_backend_tests.sh`
6. Native executable tests: `bash claw/tests/native/run_native_tests.sh`
7. CLI build-status tests: `bash claw/tests/cli/run_cli_status_tests.sh`

A successful run ends with `All C@ test suites passed successfully!`. The scripts use `set -euo pipefail` where applicable and return non-zero on missing tools/compiler, unexpected diagnostics, golden-output differences, or failed runtime assertions.

## Running one suite

All runners accept the same `CLAW_EXE` environment variable. For example:

```bash
export CLAW_EXE="$PWD/claw/build-ci/claw"
bash claw/tests/frontend/run_frontend_tests.sh
bash claw/tests/oir/run_oir_tests.sh
bash claw/tests/backend/run_backend_tests.sh
bash claw/tests/native/run_native_tests.sh
bash claw/tests/cli/run_cli_status_tests.sh
bash claw/tests/docs/run_doc_tests.sh
python3 claw/tests/docs/run_doc_command_tests.py
```

On UCRT64, use the `.exe` path instead:

```bash
export CLAW_EXE="$(cygpath -u "$PWD")/claw/build-ci/claw.exe"
```

If `CLAW_EXE` is unset, runners look for the conventional `claw/build/claw`, then `claw/build-ucrt64-clang/claw.exe` (and the legacy `claw-codex.exe` fallback). Explicitly setting it is recommended for clean CI builds.

The old compatibility entry point remains available:

```bash
bash claw/test/run_frontend_tests.sh
```

## Artifacts and debugging

Test artifacts are intentional evidence, not disposable build noise:

- Backend tests create and retain `.ll` files under `claw/tests/backend/artifacts/`. Compare these files when an LLVM assertion or golden expectation fails.
- Native tests create and retain `.ll` and `.exe` files under `claw/tests/native/artifacts/`. Use the executable and its captured output to distinguish code-generation failures from runtime failures.
- The compiler build bundles `runtime/native_runtime.c` beside the executable through the CMake post-build command. A missing runtime copy can indicate an incomplete or stale build directory.

Do not delete artifacts as part of a normal test run or hide them in a documentation change. If a clean rerun is required, remove only the relevant artifact directory, rerun the test, and mention that cleanup in the report. Build directories such as `claw/build-ci` are local/generated and should not be committed.

For a failing test, record:

1. platform and shell (`Linux` or `MSYS2 UCRT64`);
2. compiler, CMake, Ninja, Python, and `diff` versions as applicable;
3. the exact `CLAW_EXE` value (use a repository-relative description if the path is sensitive);
4. the exact suite command and exit status;
5. the first diagnostic mismatch or failing fixture;
6. relevant `.ll`/`.exe` artifacts and whether the failure reproduces after a clean build.

## Adding or changing tests

Place a regression at the narrowest matching layer and keep fixtures deterministic. Frontend tests should assert accepted/rejected source and diagnostic substrings. Backend tests should assert the relevant LLVM shape without over-constraining unrelated formatting. Native tests should assert observable behavior and stable output. Documentation and CLI tests should preserve their expected Markdown or status text exactly.

When behavior is intentionally partial, test the supported boundary and a representative rejection. Update the appropriate documentation and add a focused changelog entry. Do not use a test update to silently change PRD authority or mark an unimplemented feature complete.
