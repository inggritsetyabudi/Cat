# AGENTS.md

This repository is the language project for **C@** and the compiler implementation in `claw/`.

## Primary Rule

Work from the approved decision documents in `cat-northstar/`:

1. `cat-northstar/PRD.md` — approved language-design decisions.
2. `cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md` — PRD update plan and acceptance evidence.
3. `cat-northstar/ROADMAP-v1-plus.md` — post-v1 sequencing and explicitly staged proposals.

The former revised PRDs are preserved under `cat-northstar/legacy/`; `ideas/` is historical background only. Neither may override the approved documents above. Preserve the source documents' own version/staging boundaries; do not silently promote a post-v1 item into v1.

Design approval is not evidence of implementation. Confirm implementation status from compiler code, tests, and `changelog/`; do not infer it from `[sudah]` labels in legacy documents.

If documents conflict:
- prefer the latest explicit user decision represented in `cat-northstar/`
- if it remains unresolved, mark the relevant design point with `[conflict]` and explain why
- do not silently invent a third design

## Language Direction

C@ is aiming for:
- native performance
- explicit ownership
- implicit lifetimes
- no GC
- explicit unsafe boundaries
- diagnostics that teach without exposing lifetime jargon

Current non-negotiable design rules:
- revised syntax only: `val`, `var`, `ref`, `ref mut`, `return`, `if`, `else`, `Result[T, E]`, `Maybe[T]`, `try`, `pick`
- do not reintroduce legacy syntax such as `hold`, `slot`, `give`, `look`, `edit`, `when`, `otherwise`, `realm`, or generic `of`
- normal `shape` declarations may not store borrowed fields
- `Result[T, E]` and `Maybe[T]` are must-use
- `Anchor[T]` is a stable owned heap cell, not a smart-pointer family
- `raw` is explicit and must stay auditable

## Implementation Standard

Do not implement features half-way. For any meaningful language feature, finish the relevant path end-to-end where applicable:
- parser / AST
- semantic analysis
- ownership / borrow checking
- IR lowering
- backend or native flow if the feature reaches codegen
- tests
- docs

If a feature is intentionally staged, document the boundary clearly and keep tests honest about the supported subset.

## Repo Layout

Important directories:
- `claw/src/parser/`
- `claw/src/analysis/`
- `claw/src/ir/`
- `claw/src/codegen/`
- `claw/src/driver/`
- `claw/src/workspace/`
- `claw/runtime/`
- `claw/tests/frontend/`
- `claw/tests/backend/`
- `claw/tests/native/`
- `claw/test/`
- `cat-northstar/`
- `cat-northstar/legacy/`
- `changelog/`

The outer `README.md` is the professional user-facing language overview. Keep it aligned with the revised surface and the actual supported subset.

## Build And Test

Use the MSYS2 UCRT64 toolchain. The most reliable flow is to build with CMake and run the scripts through UCRT64 bash.

Build:
```bash
C:/msys64/ucrt64/bin/cmake.exe --build claw/build-ucrt64-clang --target claw -- -j 4
```

Frontend tests:
```bash
bash claw/tests/frontend/run_frontend_tests.sh
```

Backend tests:
```bash
bash claw/tests/backend/run_backend_tests.sh
```

Native tests:
```bash
bash claw/tests/native/run_native_tests.sh
```

All suites at once:
```bash
bash claw/tests/run_all_tests.sh
```

The old entry point `bash claw/test/run_frontend_tests.sh` remains as a compatibility shim.

When invoking from PowerShell, prefer launching through MSYS2 UCRT64 shell if plain `bash.exe` behaves badly.

## Artifacts

Do not remove the intentional test artifacts behavior:
- backend tests keep `.ll` files in `claw/tests/backend/artifacts/`
- native tests keep `.ll` and `.exe` files in `claw/tests/native/artifacts/`

These are part of the workflow for inspection and debugging.

## Documentation Discipline

Use `cat-northstar/FEATURE-STATUS.md` as the canonical G2 ledger: exactly one label per independently testable slice, `[diusulkan]` for unratified design, `[diputuskan]` for ratified semantics not yet accepted by the compiler, or `[diterima compiler]` for the implemented/tested slice only. Record implementation scope, evidence, unresolved items, and non-compiler checks separately. Never infer bug-free or production-grade quality from compiler acceptance.
Historical `[sudah]`, `[parsial]`, ✅/🟦, and version targets retain their explanatory history but are not additional canonical labels. Staged ratified semantics are not new proposals. Explain genuine conflicts without inventing ratification.
Proactive language-design reasoning and proposals are authorized; approved decisions and PRDs remain authoritative. Clearly mark new proposals, and obtain Setya's ratification before implementing material semantic changes.

Do not claim a feature is complete in docs unless the implementation and tests actually support it.

## Current Implemented Highlights

At the time of this file:
- revised syntax is active
- workspace entry validation is active
- `Result[T, E]`, `Maybe[T]`, `try`, and `pick` are active
- path-based borrow checking is active
- `Vec` element borrows require `Span`
- `scope` and scoped refs are active
- local `view shape[s]` borrowed aggregates are active
- `Anchor.new(value)` and `anchor.get()` are active
- borrowed fields in normal `shape` are rejected
- must-use for `Result` and `Maybe` statement values is enforced
- LLVM IR and native `.exe` generation exist for the supported subset

## Near-Term Roadmap

Follow the sequencing and acceptance criteria in `cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md` and `cat-northstar/ROADMAP-v1-plus.md`. Do not treat post-v1 roadmap items as current implementation requirements. Record completed implementation work in `changelog/` only after verifying the corresponding code and tests.
