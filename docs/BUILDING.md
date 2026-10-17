# Building Claw

Claw is the C++20 compiler for C@. Build the compiler first, then use it to check or compile `.cat` programs. These are separate steps: CMake's build configuration controls Claw itself; it does not select a language-level build profile for Cat programs.

## Toolchain requirements

| Tool | Purpose |
| --- | --- |
| C++20-capable compiler | Build Claw; the current CI uses `clang++` |
| CMake 3.20+ | Configure the compiler build |
| Ninja | Build generator used by CI and these instructions |
| `clang` on `PATH` | Compile emitted LLVM IR and the C11 runtime into native programs |
| Bash, Python 3, `diff`, standard shell utilities | Run the repository's regression suites |

The automated matrix in [ci.yml](../.github/workflows/ci.yml) uses Ubuntu 24.04 and Windows with MSYS2 UCRT64. Other hosts and compilers are not established by that matrix. Claw emits textual LLVM IR; the current build does not require linking LLVM development libraries into the compiler.

All commands below run from the repository root, not from `claw/`.

## Linux

On Debian/Ubuntu, install the tools used by the workflow plus the test utilities:

```bash
sudo apt-get update
sudo apt-get install --yes clang cmake ninja-build python3 diffutils
```

Configure a new Release build directory and compile:

```bash
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4
export CLAW_EXE="$PWD/claw/build-ci/claw"
```

`CLAW_EXE` is a test-runner setting, not a compiler configuration flag. Setting it explicitly prevents a runner from picking an older binary in another build directory.

```bash
bash claw/tests/run_all_tests.sh
```

For a compiler development build, use a separate directory such as `claw/build-dev` with `-DCMAKE_BUILD_TYPE=Debug`. Do not reuse a CMake cache after changing toolchains. Native Cat builds still invoke `clang -O2` in the current implementation.

## Windows: MSYS2 UCRT64

Use the **MSYS2 UCRT64 shell**, not a plain PowerShell or a different MSYS2 environment, for the commands in this section. The CI installs the following packages:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-clang \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  diffutils python
```

Then, from the repository root in that shell:

```bash
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4
export CLAW_EXE="$PWD/claw/build-ci/claw.exe"
bash claw/tests/run_all_tests.sh
```

Check `which clang clang++ cmake ninja` if paths resolve outside `/ucrt64/bin`. Avoid mixing UCRT64 tools with a different MinGW runtime. The existing [agent instructions](../AGENTS.md) also record a Windows absolute-path build invocation for an already configured `build-ucrt64-clang` directory.

## Compile a Cat program

Create a separate working directory and save this as `hello.cat`:

```cat
fn main() -> Int32 {
    println("Hello, Cat!")
    return 0
}
```

With `CLAW_EXE` still set to the absolute compiler path:

```bash
"$CLAW_EXE" check /path/to/hello.cat
"$CLAW_EXE" build /path/to/hello.cat /path/to/hello.exe
/path/to/hello.exe
```

Replace the illustrative paths with your actual paths; quote any path containing spaces. Claw appends `.exe` when an explicit output has no extension. Without an explicit output, it creates `build/<name>.exe` under the loaded package root, using the manifest project name if present or the entry source stem otherwise. A sibling `.ll` file is retained for inspection. On Linux, the output is a Linux native binary despite the `.exe` name.

For a structured workspace, put `main.cat` at its root and run `check`, `validate`, or `build` on the directory or `claw.toml`. A minimal manifest is:

```toml
[project]
name = "hello"
version = "0.1.0"
edition = "2025"

[dependencies]
```

The manifest's project version is not the compiler's release version. See the [language reference](../SPEC.md#modules-and-workspaces) and [CLI reference](CLI.md) for entry rules and input behavior.

## Runtime packaging

The CMake target copies [native_runtime.c](../claw/runtime/native_runtime.c) to `runtime/native_runtime.c` next to the built compiler. Native builds locate that source relative to the compiler and pass it to `clang` with the emitted LLVM IR. Keep the executable and its adjacent `runtime/` directory together when moving a build. This is a source-build workflow, not an installer or a claim of a supported release distribution.

## Common failures

| Symptom | Check |
| --- | --- |
| CMake cannot find a C++ compiler | Install the toolchain, inspect `PATH`, and configure a fresh build directory. |
| A test uses the wrong binary | Export an absolute `CLAW_EXE` pointing to this build, including `.exe` on Windows. |
| Native build cannot launch `clang` | Run `clang --version` in the same shell; Claw searches `PATH` when building programs. |
| Native runtime cannot be found | Restore the bundled `runtime/native_runtime.c` beside the compiler or rebuild the target. |
| `check` succeeds but `llvm` or `build` fails | Frontend acceptance is broader than the tested native subset; inspect [status](STATUS.md) and the generated diagnostics/IR. |
| Workspace has an entry error | Ensure root `main.cat` contains exactly one parameterless `main` returning `Unit` or `Int32`. |
| Tests fail only on Windows output bytes | Preserve the existing platform-aware assertions; UCRT text stdout can expand LF to CRLF. Do not globally weaken byte checks. |

Report the exact command, tool versions, compiler revision, diagnostic, and smallest reproducer. [Testing](TESTING.md) explains suite-specific artifacts; [Contributing](../CONTRIBUTING.md) explains change requirements.
