# Claw command-line reference

Claw exposes a small command interface. The commands below reflect the [current driver](../claw/src/driver/main.cpp), not the complete tooling planned in the roadmap. Use the compiler path produced by [Building Claw](BUILDING.md), or add its directory to `PATH` before using `claw` as shown here.

## Invocation

```text
claw <command> <file.cat|workspace-directory|claw.toml> [output]
claw build [--release] <input> [output]
claw doc <source.cat>
```

Use the optional output argument only with `build`. `doc` explicitly rejects an extra argument; the generic driver accepts the fourth argument for other commands but does not use it as an output destination. Redirect stdout when saving an IR or Markdown listing. There is no implemented `run`, `fmt`, `test`, package-install, or version-report command in this CLI. Calling Claw without sufficient arguments prints usage and exits with failure; do not rely on `--help` or `--version` as supported commands.

## Commands and stages

| Command | Work performed | Output |
| --- | --- | --- |
| `check` | Load input/import graph; parse; analyze types and entry rules; check ownership and borrows | Success summary on stdout; diagnostics on stderr |
| `validate` | The same checks, requiring a valid entry point | Workspace graph summary on stdout |
| `air` | Load and semantically analyze input, then render the entry module's analyzed representation | AIR text on stdout; **returns before ownership checking** |
| `oir` | Run ownership checks and lower to OIR | Ownership-aware IR on stdout |
| `lir` | Lower through OIR into backend-facing LIR | LIR on stdout |
| `llvm` | Lower through LIR and emit LLVM IR | LLVM IR on stdout; does not link an executable |
| `build` | Validate entry, check ownership, lower LLVM IR, invoke host `clang` with the native runtime | Native executable, sibling `.ll` artifact, progress and success path on stderr; stdout empty |
| `doc` | Parse one regular `.cat` file only; render documented shared top-level declarations | Deterministic UTF-8 Markdown on stdout |

Aliases are `emit-air`, `emit-oir`, `emit-lir`, `emit-llvm`, and `build-native`. They select the corresponding command above.

`check` does not require a `main` in an ordinary standalone library source. A structured workspace's root entry is validated even by `check`; `validate` and `build` always require an entry point. `main` must take no parameters and return `Unit` or `Int32`.

For structured packages rooted at `main.cat`, OIR/LIR/LLVM output includes the loaded program graph; ordinary standalone entry rendering has a narrower module context. These are internal inspection formats, not stable interchange APIs.

## Typical use

```bash
claw check ./hello.cat
claw validate ./my-app
claw build ./my-app ./out/my-app.exe
claw build --release ./my-app ./out/my-app-release.exe
claw air ./hello.cat > ./hello.air
claw oir ./hello.cat > ./hello.oir
claw lir ./hello.cat > ./hello.lir
claw llvm ./hello.cat > ./hello.ll
claw doc ./library.cat > ./library-api.md
```

The examples assume you have created the referenced source files/workspace. Passing a workspace's `claw.toml` is another way to load it. Paths are resolved relative to the current working directory unless absolute; quote paths containing spaces. Windows also has an MSYS-style path fallback for drive paths.

## Build output and runtime

Without an output argument, Claw writes `build/<name>.exe` under the package root. `<name>` comes from the manifest project name when available, otherwise the entry file stem; unsupported filename characters are sanitized. An explicit output without an extension receives `.exe`. The extension is a naming convention, not cross-compilation: Linux builds remain Linux binaries.

A sibling `.ll` file is written before linking and retained after success or linker failure. Output parent directories are created as needed. The compiler finds the bundled `runtime/native_runtime.c` relative to its executable and invokes `clang` from `PATH` with `-O0 -std=c11` by default or `-O2 -std=c11` when `--release` is specified, LLVM IR, and that runtime source. Keep the runtime bundle with the compiler. See [Building](BUILDING.md#runtime-packaging).

CMake's `Debug`/`Release` controls how Claw itself is built. Independently, plain `claw build` selects the native debug/unoptimized optimization profile and `claw build --release` selects optimized code. `--release` is a build-only flag; it may appear before or after input/output arguments. These are optimization profiles: PRD debug overflow checks, complete debug metadata, and other unaccepted language-level profile behavior are not implied. No general user-selectable linker flags are exposed.

## Compact build progress

`build` writes progress, warnings, errors, and its final summary to stderr; stdout remains empty. The six stages are Resolving (input/package/module loading), Checking (semantics/types and entry validation), Ownership (ownership/borrow checking), Lowering (OIR/LIR), Emitting (LLVM code generation), and Linking (native Clang invocation).

```text
[///:::::::::::::::::] 1/6 Checking hello::main
```

The exact counter depends on loaded modules: one resolving task, one semantic task and one ownership task per loaded unit, then three native tasks. The initial `0/6` is the single-unit baseline; the total is updated after graph resolution. It counts completed compiler tasks, not estimated time. A TTY reuses one compact line and prints diagnostics on separate lines before continuing safe work. Redirected stderr has newline-separated events, no carriage returns, and no ANSI. The bar is cyan, the counter/module dim, and stage names bold on supported POSIX terminals. `NO_COLOR` or `TERM=dumb` disables ANSI; Windows always uses plain text.

A successful debug build ends with:

```text
Finished native release [debug + unoptimized] in 0.123s
     Output /absolute/path/to/hello.exe
```

`--release` uses `[optimized]` instead. Time is measured with a monotonic clock and the output path is the actual absolute artifact path. The word `release` in the requested summary is retained even for the debug optimization profile; the bracketed profile disambiguates it.

Build diagnostics use `warning:`/`error:` headers, source locations/snippets, and available ownership labels. Other commands retain their existing stage-qualified diagnostic headers. A failed build ends with `error: build failed for ` followed by the project/input name in backticks. Compilation stops when later stages would be invalid; warnings do not prevent continuing. Internal Cat diagnostics print when discovered. POSIX Clang stderr is still captured through the existing hardened descriptor and replayed after Clang exits; live streaming and restyling of external diagnostics are not implemented.

## Documentation comments

Use `///` immediately before a top-level declaration. For example, save the complete source below as `library.cat`:

```cat
/// Adds two signed 32-bit integers.
share fn add(left: Int32, right: Int32) -> Int32 {
    left + right
}
```

`claw doc library.cat` produces:

```text
# add

Adds two signed 32-bit integers.
```

The parser removes the `///` prefix and at most one following ASCII space, joins lines with LF, and attaches them to the next top-level declaration. Blank lines and ordinary comments do not break attachment. Documentation on a non-`share` declaration or orphan documentation at EOF produces a non-fatal warning.

Only documented shared functions, shapes, and choices are rendered, in source order. Undocumented items are omitted. Fields/members, import traversal, complete signatures, and workspace-wide API generation are not implemented. `doc` performs parsing, not semantic or ownership validation: run `check` separately. An empty Markdown result can be successful when no eligible documented declarations exist.

## Diagnostics and exit status

Compiler diagnostics carry a stage, source path/span, and message; warnings are non-fatal. Successful commands return `0`. Invalid usage, unresolved input, compilation rejection, and caught native-build failures return `1`. The native driver reports whether `clang` failed to launch, exited unsuccessfully, or was terminated on POSIX, but Claw's own CLI still returns its failure status rather than promising transparent child exit-code propagation.

A `doc` parse success or an `air` result is not equivalent to `check` success. Likewise, `check` success is not native-backend certification. To demonstrate end-to-end support, include LLVM/native tests and execution evidence as described in [Testing](TESTING.md).
