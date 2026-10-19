# C@ — Cat

**Fast, Safe, Simple.**

C@ (Cat) is an in-development systems programming language with explicit ownership, borrowing, and native code generation. **Claw** is its compiler, implemented in C++20. Source files use the `.cat` extension.

Cat aims to make systems code predictable without hiding allocation, ownership, or unsafe operations. The compiler already checks real programs and produces native executables for a tested subset of the language. It is **not a completed v1.0 language or a production-readiness claim**.

## Principles

| Principle | Direction | Current boundary |
| --- | --- | --- |
| **Fast** | Native code, explicit costs, deterministic ownership, no hidden garbage collector | LLVM IR and native compilation exist; the PRD's performance targets are not established benchmark results. |
| **Safe** | Ownership, exclusive mutable borrows, initialization checks, typed errors, auditable unsafe regions | The current checker covers tested cases; general dataflow lifetime analysis and concurrency safety are not complete. |
| **Simple** | Direct syntax, explicit modules, a small set of core concepts | The approved design is ahead of implementation in several areas; supported syntax and planned syntax are documented separately. |

## A first program

Save this as `hello.cat` outside the compiler repository, or in a separate example directory:

```cat
fn add(left: Int32, right: Int32) -> Int32 {
    left + right
}

fn main() -> Int32 {
    val answer = add(20, 22)
    println("Hello, Cat!")
    println(answer)
    return 0
}
```

`val` creates an immutable binding; `var` creates a mutable binding. Functions use positional arguments and may return their final expression. `main` takes no parameters and returns `Unit` or `Int32`.

## Build and run

The commands below run from the repository root using a C++20-capable Clang, CMake 3.20 or newer, Ninja, and Bash. Native program compilation also needs `clang` on `PATH` and the bundled C runtime. See [Building Claw](docs/BUILDING.md) for Linux dependencies, Windows MSYS2 UCRT64 setup, and troubleshooting.

```bash
cmake -S claw -B claw/build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build claw/build-ci --target claw --parallel 4

# Linux; use claw.exe instead on Windows UCRT64.
export CLAW_EXE="$PWD/claw/build-ci/claw"
"$CLAW_EXE" check /path/to/hello.cat
"$CLAW_EXE" build /path/to/hello.cat ./hello.exe
./hello.exe
```

Expected program output:

```text
Hello, Cat!
42
```

The `.exe` suffix is Claw's default naming convention even on Linux; the actual binary targets the host toolchain. A successful `check` is a frontend result, not a guarantee that every accepted construct has native lowering.

## Language at a glance

| Area | Current surface |
| --- | --- |
| Bindings and functions | `val`, `var`, `fn`, explicit `return`, block-tail returns |
| Borrowing | `ref T`, `ref mut T`, move checks, path-based overlap checks |
| Control flow | `if` / `else`, `loop`, `scan`, `stop`, `skip`, exhaustive `pick` in supported patterns |
| Data | Nominal `shape`, tagged `choice`, generic types with `[]`, tested tuple and fixed-array subsets |
| Errors and optional values | Must-use `Result[T, E]` and `Maybe[T]`; `try` as a `val` initializer |
| Modules | One file per module, path-based imports, `share` visibility, root `main.cat` for workspaces |
| Text | UTF-8 string literals, Unicode scalar `Char`, validated escapes, byte-based string ranges |
| Ownership helpers | Bounded `Anchor`, scoped references, local `view shape` carriers |
| Documentation | Top-level `///` comments and single-file Markdown output with `claw doc` |

The [language reference](SPEC.md) explains syntax, restrictions, and implementation evidence. In particular, normal shapes cannot store borrowed fields, `Vec` element borrowing requires a `Span`, and general lifetime inference is not implemented.

Builtin scalar names such as `Int32` also accept ASCII case variants such as `int32`. This is a closed exception: user-defined names, generic containers, and ordinary identifiers remain case-sensitive under their naming rules.

## Workspaces

A standalone `.cat` file is enough to start. For multiple modules, use a separate workspace:

```text
my-app/
  claw.toml
  main.cat
  src/
    math.cat
    util.cat
```

Each file is one module; directories form namespaces. `share` makes declarations importable. Imports are explicit, for example `import src.math as math_ops`; `import super.{ sibling }` addresses a sibling file in the same directory. A workspace must have exactly one `fn main` in root `main.cat`. That file is entry-only: it cannot be imported, and `main` cannot be called like an ordinary function. No `modules.cat` index is required.

See [workspace and module rules](SPEC.md#modules-and-workspaces) and the [CLI reference](docs/CLI.md) for validation and build commands.

## Compiler tools

| Command | Purpose |
| --- | --- |
| `claw check <input>` | Check parsing, types, ownership, and borrows |
| `claw validate <input>` | Check a program's entry graph and entry contract |
| `claw build [--release] <input> [output]` | Lower and link a native executable |
| `claw air <input>` | Inspect analyzed AIR, before ownership checking |
| `claw oir <input>` | Inspect ownership-aware OIR |
| `claw lir <input>` | Inspect backend-facing LIR |
| `claw llvm <input>` | Emit LLVM IR |
| `claw doc <source.cat>` | Emit Markdown for documented shared top-level declarations |

`<input>` can be a source file, workspace directory, or `claw.toml`, except that `doc` accepts one source file only. The [CLI reference](docs/CLI.md) covers aliases, output paths, diagnostics, and the compact build status display. Native builds default to debug/unoptimized (`-O0`); add `--release` for optimized (`-O2`) output. These select native optimization, not complete PRD debug/release language semantics.

## Project status

The active CI matrix builds Release Claw with Clang on **Linux** and **Windows MSYS2 UCRT64**, then runs the complete test runner. These are the automated targets, not a claim of support for every operating system or LLVM target.

Implemented slices include revised syntax, workspace checks, typed errors, tested ownership and scoped-type paths, LLVM/native compilation, integer and character ranges, selected fixed arrays, string escapes, and documentation comments. The [implementation status](docs/STATUS.md) records the boundaries and points to evidence.

Still incomplete are the full approved collection and builtin surface, general dataflow borrow checking, complete raw/FFI support, broad aggregate lowering, f-strings, and the remaining release gates. **Arena remains pending/experimental**; this documentation does not promote it to a supported feature. The borrowed `Str` / owned `String` foundation from PR #21 is present on main `e7789c6`; its bounded ownership evidence and exclusions are recorded in the [G2 feature inventory](cat-northstar/FEATURE-STATUS.md), not a claim of full string-library acceptance.

## Documentation

| Document | Start here when you want to… |
| --- | --- |
| [Documentation index](docs/README.md) | Find the right guide and understand source authority |
| [Language reference](SPEC.md) | Understand the current supported surface and its limits |
| [Architecture](ARCHITECTURE.md) | Follow the compiler pipeline, runtime, and ABI boundaries |
| [Building](docs/BUILDING.md) | Configure a local toolchain and compile Claw |
| [CLI reference](docs/CLI.md) | Use commands and inspect compiler output |
| [Testing](docs/TESTING.md) | Run suites, reproduce failures, and inspect artifacts |
| [Contributing](CONTRIBUTING.md) | Propose and validate a focused change |
| [Implementation status](docs/STATUS.md) | Separate available features from remaining work |
| [Approved design](cat-northstar/README.md) | Read the PRD and staged acceptance criteria |
| [Changelog](changelog/README.md) | Trace individual implementation and documentation changes |

Existing [compiler notes](claw/README.md), [builtin audit](claw/docs/builtin-methods.md), and [safety notes](claw/docs/frontend-safety-status.md) remain available for deeper context.

## Development

```bash
# From the repository root, after the build above.
export CLAW_EXE="$PWD/claw/build-ci/claw"
bash claw/tests/run_all_tests.sh
```

On Windows UCRT64, set `CLAW_EXE` to the corresponding `claw.exe`. Tests include PRD examples, frontend/ownership checks, OIR, LLVM/backend regressions, native integration, and CLI behavior. Backend/native suites intentionally keep generated artifacts for inspection. See [Testing](docs/TESTING.md) before changing fixtures or interpreting sanitizer results.

Contributions should follow the approved design, add positive and negative evidence for their supported slice, and include a separate Markdown file in `changelog/` for every individual repository change. Start with [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).

## Design authority and license

[PRD.md](cat-northstar/PRD.md), the [v0.1–v1.0 change plan](cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md), and the [post-v1 roadmap](cat-northstar/ROADMAP-v1-plus.md) define approved direction and staging. Approval is not proof that a feature is implemented. The former `revise/` summaries are preserved under [cat-northstar/legacy/](cat-northstar/legacy/); [ideas/](ideas/README.md) is historical background, not current language authority.

Cat is licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).
