# Contributing to C@

Thank you for contributing to C@ and the Claw compiler. This repository is an active language and compiler project: a useful change is one whose design, implementation, tests, diagnostics, and documentation agree about the supported subset.

## Before you start

Read the repository rules in [`AGENTS.md`](./AGENTS.md) first. The design authority is, in order of practical use:

1. [`cat-northstar/PRD.md`](./cat-northstar/PRD.md) — approved language-design decisions.
2. [`cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md`](./cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md) — staged changes and acceptance evidence.
3. [`cat-northstar/ROADMAP-v1-plus.md`](./cat-northstar/ROADMAP-v1-plus.md) — explicitly staged post-v1 work.
4. Compiler code, tests, and [`changelog/`](./changelog/) — evidence of what is actually implemented.

The PRD describes intended design; it is not, by itself, proof that a feature is implemented. Do not promote a roadmap item, legacy syntax, or an idea into the current language without an approved decision. If approved documents conflict, follow the latest explicit decision; if the conflict is unresolved, document it as a conflict rather than inventing a third design.

For the public project overview, see [`README.md`](./README.md). The documentation also provides the [language reference](./SPEC.md), [build guide](./docs/BUILDING.md), [CLI reference](./docs/CLI.md), and [implementation status](./docs/STATUS.md).

## What makes a good change

- Keep revised syntax and current safety rules intact; do not reintroduce legacy forms.
- Complete a meaningful feature end-to-end where applicable: parser/AST, semantic analysis, ownership and borrowing, IR, native flow, tests, and docs.
- Keep staged or unsupported behavior explicit. Do not describe a feature as complete unless code and tests support the claim.
- Prefer focused changes. Avoid unrelated formatting churn and do not modify generated or intentional test artifacts merely to make a diff quiet.
- Add or update regression tests at the narrowest relevant layer, then run the complete suite when practical.
- Every individual repository change needs its own new Markdown file in `changelog/`. For documentation work, use a separate entry for each changed document. Follow the existing dated format: describe the change, its boundary, and the validation actually run. Do not recursively create changelogs for changelog entries themselves.

## Branches and pull requests

The normal contribution path is a branch with a pull request:

1. Create a focused branch from the current `main`.
2. Make the smallest coherent change, including tests and documentation.
3. Build and test locally using the commands below.
4. Inspect `git diff` and `git status`; confirm that only intended files changed.
5. Open a pull request with a concise summary, design authority, test commands, and any known limitations.
6. Address review comments and keep the branch current enough for CI to be meaningful.
7. Merge only after required review and CI checks are green.

Direct commits to `main` are an exception requiring explicit maintainer authorization for that task. They do not change the normal pull-request workflow. Automated or assisted editing should commit or push only when that action is explicitly authorized.

A useful pull request description should state:

- **Intent:** what problem the change solves.
- **Authority:** the relevant PRD section, accepted decision, issue, or explicit staged boundary.
- **Implementation:** which compiler layers or documents changed.
- **Verification:** exact commands run and their result.
- **Artifacts/limitations:** generated `.ll`/`.exe` outputs, platform differences, or tests not run.

## Local build

### Linux (Clang + Ninja)

The CI Linux job uses Ubuntu 24.04, Clang, CMake, and Ninja. From the repository root:

```bash
sudo apt-get update
sudo apt-get install --yes --no-install-recommends clang cmake ninja-build
rm -rf claw/build-ci
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4
```

Use the resulting compiler explicitly when running tests:

```bash
export CLAW_EXE="$PWD/claw/build-ci/claw"
bash claw/tests/run_all_tests.sh
```

### Windows (MSYS2 UCRT64)

Use an MSYS2 **UCRT64** shell, not a generic MSYS shell. Install or update the same toolchain used by CI:

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-clang \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  diffutils \
  python
```

From the repository root in that UCRT64 shell:

```bash
rm -rf claw/build-ci
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4
export CLAW_EXE="$(cygpath -u "$PWD")/claw/build-ci/claw.exe"
printf 'Using compiler: %s\n' "$CLAW_EXE"
bash claw/tests/run_all_tests.sh
```

If starting from PowerShell, open the installed MSYS2 UCRT64 launcher, then run the build/test commands in that shell. A generic `bash.exe` login shell does not by itself select UCRT64. For a standard installation, the launcher is:

```powershell
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64
```

CI sets `core.autocrlf=input` before checkout. Prefer a repository-local setting when needed (`git config --local core.autocrlf input`), inspect existing line endings, and avoid unrelated normalization or global configuration changes.

For a quick one-command build against the repository's existing UCRT64 build directory, the compatibility command documented by `AGENTS.md` is:

```bash
C:/msys64/ucrt64/bin/cmake.exe --build claw/build-ucrt64-clang --target claw -- -j 4
```

## Tests and evidence

The complete test entry point is [`claw/tests/run_all_tests.sh`](./claw/tests/run_all_tests.sh). It runs documentation tests, documentation-command tests, frontend tests, OIR golden tests, backend LLVM tests, native executable tests, and CLI build-status tests. Layer-specific commands and artifact handling are documented in [`docs/TESTING.md`](./docs/TESTING.md).

Before submitting, run at least the suite relevant to the change and preferably the complete suite. Set `CLAW_EXE` when the compiler is not in the script's default build location. The scripts fail fast and return a non-zero status for missing executables, unexpected diagnostics, output mismatches, or failed assertions.

Backend tests intentionally retain LLVM output in `claw/tests/backend/artifacts/`. Native tests intentionally retain LLVM and executable output in `claw/tests/native/artifacts/`. These directories are generated locally, not published documentation links. Inspect these files when diagnosing a failure; do not treat their presence as accidental clutter.

## Review checklist

- [ ] The change follows `AGENTS.md` and an approved design source.
- [ ] Current syntax and staged boundaries are preserved.
- [ ] Tests cover both the expected behavior and important rejection/diagnostic cases.
- [ ] Linux and Windows UCRT64 commands are applicable, or the platform limitation is explained.
- [ ] `CLAW_EXE`, generated artifacts, and environment assumptions are documented where relevant.
- [ ] Documentation links resolve relative to the repository.
- [ ] The diff contains no unrelated source, design-status, or generated changes; each document has its own changelog entry.
- [ ] No commit was made unless explicitly authorized.
