# C@ Compiler Architecture

**Repository baseline:** `6b17b68` (main)

**Scope:** the implementation that is present in this repository on 2026-10-17. This document describes the current compiler, not the complete language design or a production-readiness claim.

## Status and sources of truth

C@ is an in-progress systems language and compiler. The approved language direction is documented in [`cat-northstar/PRD.md`](./cat-northstar/PRD.md), with version sequencing and acceptance boundaries in [`cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md`](./cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md) and post-v1 staging in [`cat-northstar/ROADMAP-v1-plus.md`](./cat-northstar/ROADMAP-v1-plus.md). Those documents describe decisions and targets; implementation status is established here from compiler code, tests, and [`changelog/`](./changelog/).

The currently verified implementation includes a revised syntax subset, workspace entry validation, semantic analysis, path-oriented ownership checks, AIR/OIR/LIR lowering, LLVM IR emission, and native executable generation for tested cases. It does **not** implement every ratified or proposed language feature. In particular, the full collection/builtin surface, broad lifetime inference, Arena, and broader raw/FFI lowering remain bounded or incomplete. See the current status notes in [`README.md`](./README.md) and [`claw/README.md`](./claw/README.md).

## System shape

```text
C@ source (.cat) / workspace (claw.toml)
        |
        v
  input resolution and package/module loading
        |
        v
  lexer -> parser -> AST
        |
        v
  semantic analysis (types, names, imports, visibility, constructs)
        |
        +--> AIR   (`claw air`)
        |
        v
  ownership / borrow checking (supported checked paths)
        |
        v
  OIR (typed/lowered control flow, bindings, drops, choices, try)
        |
        v
  LIR (backend-facing blocks, layouts, ABI and safety metadata)
        |
        v
  LLVM IR
        |
        +--> `claw llvm` output
        |
        v
  clang -O2 + bundled native_runtime.c
        |
        v
  native executable (`.exe` on the current documented flow)
```

The stages are implemented in [`claw/src/`](./claw/src). The command driver is [`claw/src/driver/main.cpp`](./claw/src/driver/main.cpp); it owns command dispatch, project validation, diagnostics, and orchestration. The native link boundary is [`claw/src/driver/native_build.cpp`](./claw/src/driver/native_build.cpp).

## Frontend and workspace ownership

### Input and workspace loading

[`claw/src/workspace/`](./claw/src/workspace/) resolves a file or workspace path, parses `claw.toml` when present, discovers the root `main.cat`, and constructs the loaded project and module graph. The current workspace contract is deliberately explicit:

- the root `main.cat` is the entry module;
- exactly one valid `fn main` is required in the entry unit;
- `main.cat` is not a normal importable module surface;
- each `.cat` file is one module, with parent directories forming its namespace;
- `share` controls importable declarations; imports are path-based and may require aliases;
- module cycles, invalid `super` paths, and private type/item leakage are rejected by the frontend tests.

This is a file/module graph, not a separate `modules.cat` manifest model. The loader and config parser are separate from parsing a source file so `validate` can check the workspace entry graph without requiring native code generation.

### Lexer, parser, AST

[`claw/src/lexer/`](./claw/src/lexer/) tokenizes source and handles literal escape support. [`claw/src/parser/`](./claw/src/parser/) parses one file into the AST declared in [`claw/src/ast/ast.h`](./claw/src/ast/ast.h). The parser owns grammar recovery and constructs declarations, types, statements, expressions, `if`/`pick`, `try`, scopes, imports, shapes, choices, contracts, and implementations. Parser diagnostics retain source spans for later formatting.

### Semantic analysis

[`claw/src/analysis/sema.cpp`](./claw/src/analysis/sema.cpp), together with [`types.cpp`](./claw/src/analysis/types.cpp), [`choice_utils.cpp`](./claw/src/analysis/choice_utils.cpp), [`builtin_methods.cpp`](./claw/src/analysis/builtin_methods.cpp), and [`numeric_literals.cpp`](./claw/src/analysis/numeric_literals.cpp), resolves names and types and validates language rules. This includes nominal identity across modules, visibility, generic arguments, choice/pattern rules, `Result`/`Maybe` handling, `try` constraints, must-use checks, shape restrictions, and the currently supported builtin methods. Diagnostics are structured in [`claw/src/diagnostics/`](./claw/src/diagnostics/).

Semantic analysis is instantiated per loaded unit, with imported bindings and target information supplied by the driver. It is therefore not a single global AST pass: workspace loading establishes the graph and each unit's analyzer validates its local source against that graph.

## Ownership and borrowing analysis

Ownership analysis is implemented by [`claw/src/analysis/ownership.h`](./claw/src/analysis/ownership.h) and [`ownership.cpp`](./claw/src/analysis/ownership.cpp). The checker tracks binding identity and ownership-sensitive uses, including moves, drops, returned values, `Result`/`try` consumption, path-based field overlap, mutable borrowing, and escape diagnostics for the supported subset. [`scope.h`](./claw/src/analysis/scope.h) and [`scope.cpp`](./claw/src/analysis/scope.cpp) carry scope/view-shape information used by scoped references.

The user-facing model aims for explicit ownership with implicit lifetimes, no GC, and explicit unsafe boundaries. The implementation should be read as a bounded checker, not as proof that every ownership scenario is covered. Current validated paths include `ref`, `ref mut`, scoped references, local borrowed aggregates (`view shape[s]`), tested direct scoped types and nested forms such as `Box[Name[s]]`, `Anchor.new(value)`, and relevant negative tests. Generic shape literals in nested scoped forms require an expected concrete type; general lifetime inference is not implemented. `Arena` remains design direction rather than a fully implemented ownership facility.

The code and test layout intentionally distinguish semantic analysis from ownership analysis. For example, the current `air` path can emit after semantic analysis without running the ownership pass, while the checked frontend/OIR/native paths exercise ownership-sensitive behavior. Do not infer ownership coverage from an AIR dump alone.

## Intermediate representations and backend

The IR layers are explicit and inspectable:

- **AIR (Analyzed IR):** [`claw/src/ir/air.h`](./claw/src/ir/air.h) and `air.cpp` print a representation close to the analyzed AST. It is useful for frontend/semantic inspection.
- **OIR (Ownership/operation-oriented IR):** [`claw/src/ir/oir.h`](./claw/src/ir/oir.h) and `oir.cpp` represent typed operations such as bindings, stores, returns, drops, branches, scans, picks, `try`, choice construction, and calls. OIR carries binding keys and ownership-relevant operations used by later stages.
- **LIR (lowered/backend IR):** [`claw/src/ir/lir.h`](./claw/src/ir/lir.h) and `lir.cpp` provide backend-facing basic blocks, calls, control flow, layouts, linkage, safety tags, and ABI metadata. The OIR formatter and golden tests make this lowering inspectable.

[`claw/src/codegen/`](./claw/src/codegen/) lowers the backend representation to LLVM IR. The implementation is split between LLVM orchestration, type/layout lowering, values, control flow, and emission. The compiler emits textual LLVM IR; `claw llvm` exposes that artifact directly and native builds preserve the generated `.ll` file beside the requested output when linking fails.

## Runtime and ABI boundary

The native support library is [`claw/runtime/native_runtime.c`](./claw/runtime/native_runtime.c). It contains the C-side support used by generated programs, including byte/integer output, heap allocation/free for `Anchor`, buffer/slice representations, and arena-related runtime support present in the source. The CMake build copies this file beside the compiler executable under `runtime/native_runtime.c`; the native driver also searches executable-adjacent and repository-relative candidates so a build can run outside the repository working directory. This packaging and regression behavior is recorded in [`changelog/2026-10-09.Native-Runtime-Discovery.md`](./changelog/2026-10-09.Native-Runtime-Discovery.md).

The type system records layout and ABI metadata in [`claw/src/analysis/types.h`](./claw/src/analysis/types.h). Current pass categories are `Void`, `Scalar`, `Direct`, `Indirect`, and `Borrow` (with `Unknown` for unresolved cases). LIR carries pass kinds for parameters and return values. LLVM type lowering uses that metadata to choose direct values, indirect storage/pointers, or borrow-pointer forms; aggregate and choice layouts are represented explicitly rather than treated as an undocumented C ABI.

This is an internal compiler ABI for the generated code, not a promise of a stable external C ABI. `foreign c` and `raw` are explicit language boundaries, and the documented native support is limited to the currently typed/tested subset. Do not treat the runtime C functions, generated symbol names, or LIR pass categories as a versioned public ABI.

## Native toolchain

The project is built as a C++20 CMake target. [`claw/CMakeLists.txt`](./claw/CMakeLists.txt) builds the `claw_frontend` library from frontend, analysis, IR, codegen, workspace, and diagnostics sources, then links the `claw` executable with `Threads::Threads`. The current [CI matrix](./.github/workflows/ci.yml) uses Clang, CMake, and Ninja on Linux and Windows MSYS2 UCRT64. See [Building](./docs/BUILDING.md) for both workflows.

For `claw build`, the driver performs input resolution, project loading, entry validation, semantic checks, ownership checks, native LLVM emission, and then invokes `clang -O2 -std=c11` with the generated `.ll` and the located `native_runtime.c`. The current driver has one native optimized profile; it does not expose a debug/unoptimized profile or profile-selection flag. The output path defaults according to the project and receives an `.exe` extension when no extension is supplied.

The compiler itself does not embed a general-purpose LLVM library API in this repository; it emits LLVM text and delegates final compilation/linking to the `clang` executable found on `PATH`. Toolchain availability and target behavior therefore remain environment-dependent.

## Tests and evidence

The active runners are intentionally layered:

- [`claw/tests/docs/`](./claw/tests/docs/) checks tagged executable/documentation examples and the `doc` command;
- [`claw/tests/frontend/`](./claw/tests/frontend/) covers parsing, semantic rules, module/workspace behavior, diagnostics, and negative cases;
- [`claw/tests/oir/`](./claw/tests/oir/) checks OIR formatter/golden behavior;
- [`claw/tests/backend/`](./claw/tests/backend/) checks LLVM IR markers and backend regressions;
- [`claw/tests/native/`](./claw/tests/native/) builds and executes supported native fixtures, including tested workspace and scoped-type paths;
- [`claw/tests/cli/`](./claw/tests/cli/) checks build-status CLI behavior;
- [`claw/tests/run_all_tests.sh`](./claw/tests/run_all_tests.sh) runs the documented suites in order.

Backend tests intentionally retain `.ll` artifacts, and native tests retain generated `.ll` and executable artifacts for inspection. Test success demonstrates the covered fixtures and assertions; it is not evidence that the language is complete or that untested programs have production guarantees.

## Known boundaries

The following boundaries are intentional and should remain visible in architecture discussions:

1. The approved PRD contains staged items and proposals that are not all implemented. Doc-test labels distinguish executable examples from proposals; they do not turn a proposal into compiler support.
2. Native coverage is a supported subset: single-file programs, simple root-entry workspaces, tested `Maybe`/`Result` paths, runtime printing, direct arithmetic/calls, and validated scoped-type fixtures. Full imported-workspace native coverage, broad aggregate construction, the complete collection/builtin surface, and broad raw/FFI lowering are not claimed.
3. General lifetime inference and every aggregate/escape pattern are not implemented; scoped support is validated only in documented cases.
4. Arena is not fully implemented, and the runtime source containing arena support should not be read as proof of a complete language-level Arena feature.
5. The native pipeline depends on an external `clang` executable and a compatible C runtime/toolchain. No stable public ABI is promised.
6. Architecture descriptions must be checked against code, tests, and changelog entries as the compiler evolves.

## Useful entry points

- [`README.md`](./README.md) — user-facing status and language overview.
- [`AGENTS.md`](./AGENTS.md) — repository rules and verification commands.
- [`claw/README.md`](./claw/README.md) — compiler commands and current native scope.
- [`claw/docs/frontend-safety-status.md`](./claw/docs/frontend-safety-status.md) — safety verification notes.
- [`changelog/`](./changelog/) — dated implementation evidence and regressions.
