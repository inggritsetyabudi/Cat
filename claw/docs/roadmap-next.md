# Roadmap Next

## Current Position

The compiler has passing frontend, LLVM/backend, and native suites for its supported subset. Scoped support is bounded and validated for direct forms such as `Name[s]` and nested forms such as `Box[Name[s]]`, including tested function signatures, field access, LLVM ABI paths, and native workspace execution. Generic shape literals require an expected concrete type. General lifetime inference remains unimplemented.

Do not generalize those results to all generic or lifetime patterns, and do not claim the full built-in method catalog is available end-to-end.

## Immediate Workstream: Receiver-First Built-in Method Dispatch

Use one dedicated branch for the workstream and its follow-ups. First reconcile `claw/docs/builtin-methods.md` and PRD section 31 with semantic registration, ownership/borrow behavior, LLVM lowering, and actual frontend/backend/native coverage. Then select one bounded receiver family and finish a vertical slice across semantics, IR/LLVM, native execution, and positive/negative tests.

Current source registers more methods than LLVM can lower, and the existing native fixture covers only a small subset. Keep every method outside the validated vertical slice explicitly marked as planned or incomplete.

## After the First Dispatch Slice

1. Extend receiver families only in complete, independently tested slices on the same workstream branch.
2. Continue revised type/collection cleanup and improve diagnostics without reviving removed syntax.
3. Broaden native and workspace coverage where tests demonstrate support.
4. Defer general lifetime inference, `Arena`, advanced raw/FFI contracts, and optimization work until separately designed and validated.

## Validation Discipline

For language changes, run real Cat fixtures through frontend checks, LLVM emission/verifier, and native build/execution where applicable. Add both positive and negative cases; documentation and PRD `[sudah]` markers must not exceed the behavior those tests establish.
