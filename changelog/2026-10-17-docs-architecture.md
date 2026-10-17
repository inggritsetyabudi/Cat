# Compiler architecture reference (2026-10-17)

**Status:** documentation-only addition at implementation baseline `6b17b68`.

## Changes

Added [ARCHITECTURE.md](../ARCHITECTURE.md) covering module loading, lexer/parser/AST, semantic and ownership analysis, AIR/OIR/LIR, LLVM emission, native linking, runtime bundling, ABI categories, and layered tests. The diagram and prose explicitly place AIR emission before ownership checking. Toolchain notes reflect the Linux and Windows UCRT64 CI matrix.

## Validation

Cross-checked the document with current driver, CMake, workspace, analysis, IR/codegen, and runtime paths. Checked relative links and anchors. A clean Linux Release build and the complete test runner passed; this supports the described tested paths, not untested language completeness.

## Boundary

No compiler behavior, canonical design, acceptance marker, stable ABI contract, or Arena implementation changed. Existing compiler/design notes were preserved.
