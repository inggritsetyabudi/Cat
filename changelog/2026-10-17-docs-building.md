# Source-build guide (2026-10-17)

**Status:** documentation-only addition at implementation baseline `6b17b68`.

## Changes

Added [docs/BUILDING.md](../docs/BUILDING.md) with C++20/CMake/tool requirements, CI-aligned Linux and Windows MSYS2 UCRT64 instructions, explicit `CLAW_EXE` usage, a complete Cat program, workspace manifest example, runtime bundling, output naming, and troubleshooting. It separates the compiler's CMake configuration from Cat's currently fixed native `-O2` path.

## Validation

Ran a clean Linux Release configure/build with Clang and Ninja, then the complete repository test runner; all passed. The guide's Cat program passed check, native build, and execution. Cross-checked Windows package/build commands against the current CI workflow; no local Windows execution is claimed. Checked relative links and anchors.

## Boundary

No installer, binary release, alternate target certification, compiler behavior change, or milestone completion is introduced.
