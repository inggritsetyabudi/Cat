# Claw command-line reference

Claw exposes a small command interface. The commands below reflect the [current driver](../claw/src/driver/main.cpp), not the complete tooling planned in the roadmap. Use the compiler path produced by [Building Claw](BUILDING.md), or add its directory to `PATH` before using `claw` as shown here.

## Invocation

```text
claw <command> <file.cat|workspace-directory|claw.toml> [output]
claw doc <source.cat>
```

Use the optional output argument only with `build`. `doc` explicitly rejects an extra argument; the generic driver accepts the fourth argument for other commands but does not use it as an output destination. Redirect stdout when saving an IR or Markdown listing. There is no implemented `run`, `fmt`, `test`, package-install, profile-selection, or version-report command in this CLI. Calling Claw without sufficient arguments prints usage and exits with failure; do not rely on `--help` or `--version` as supported commands.

## Commands and stages

| Command | Work performed | Output |
| --- | --- | --- |
| `check` | Load input/import graph; parse; analyze types and entry rules; check ownership and borrows | Success summary on stdout; diagnostics on stderr |
| `validate` | The same checks, requiring a valid entry point | Workspace graph summary on stdout |
| `air` | Load and semantically analyze input, then render the entry module's analyzed representation | AIR text on stdout; **returns before ownership checking** |
| `oir` | Run ownership checks and lower to OIR | Ownership-aware IR on stdout |
| `lir` | Lower through OIR into backend-facing LIR | LIR on stdout |
| `llvm` | Lower through LIR and emit LLVM IR | LLVM IR on stdout; does not link an executable |
| `build` | Validate entry, check ownership, lower LLVM IR, invoke host `clang` with the native runtime | Native executable, sibling `.ll` artifact, success path on stdout |
| `doc` | Parse one regular `.cat` file only; render documented shared top-level declarations | Deterministic UTF-8 Markdown on stdout |

Aliases are `emit-air`, `emit-oir`, `emit-lir`, `emit-llvm`, and `build-native`. They select the corresponding command above.

`check` does not require a `main` in an ordinary standalone library source. A structured workspace's root entry is validated even by `check`; `validate` and `build` always require an entry point. `main` must take no parameters and return `Unit` or `Int32`.

For structured packages rooted at `main.cat`, OIR/LIR/LLVM output includes the loaded program graph; ordinary standalone entry rendering has a narrower module context. These are internal inspection formats, not stable interchange APIs.

## Typical use

```bash
claw check ./hello.cat
claw validate ./my-app
claw build ./my-app ./out/my-app.exe
claw air ./hello.cat > ./hello.air
claw oir ./hello.cat > ./hello.oir
claw lir ./hello.cat > ./hello.lir
claw llvm ./hello.cat > ./hello.ll
claw doc ./library.cat > ./library-api.md
```

The examples assume you have created the referenced source files/workspace. Passing a workspace's `claw.toml` is another way to load it. Paths are resolved relative to the current working directory unless absolute; quote paths containing spaces. Windows also has an MSYS-style path fallback for drive paths.

## Build output and runtime

Without an output argument, Claw writes `build/<name>.exe` under the package root. `<name>` comes from the manifest project name when available, otherwise the entry file stem; unsupported filename characters are sanitized. An explicit output without an extension receives `.exe`. The extension is a naming convention, not cross-compilation: Linux builds remain Linux binaries.

A sibling `.ll` file is written before linking and retained after success or linker failure. Output parent directories are created as needed. The compiler finds the bundled `runtime/native_runtime.c` relative to its executable and invokes `clang` from `PATH` with `-O2 -std=c11`, LLVM IR, and that runtime source. Keep the runtime bundle with the compiler. See [Building](BUILDING.md#runtime-packaging).

CMake's `Debug`/`Release` controls how Claw itself is built. The Cat native driver currently has one optimized profile, `-O2`; it does not provide a debug/unoptimized language profile or user-selectable linker flags through this command line.

## C@ Forge progress

`build` writes progress to stderr and preserves the final `Built native executable: ...` stdout message on success. The display tracks six phases: input resolution, package graph loading, type/entry graph checking, entry verification, ownership checking, and LLVM lowering/native linking.

The status key is `=` complete, `>` active, and `.` waiting; failure uses `!`. The lowering/linking phase is explicitly **indeterminate**, not an estimated percentage. Interactive POSIX terminals can display a spinner and ANSI colors; `NO_COLOR` or `TERM=dumb` disables styling. Windows uses plain text and a static indeterminate label. Redirecting output should not be interpreted as disabling compilation checks.

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
