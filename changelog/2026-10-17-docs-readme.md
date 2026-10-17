# Professional project README (2026-10-17)

**Status:** documentation-only update against implementation baseline `6b17b68`; no language version, release, or milestone change.

## Changes

Rewrote [README.md](../README.md) as the public entry point for Cat/C@: principles, complete first program, source-build quickstart, workspace rules, current CLI including `doc`, implementation boundaries, documentation map, contribution path, and license. Replaced incomplete example fragments and undeclared imports with a self-contained native program. Detailed rules now link to the companion guides; existing design archives and compiler notes remain intact.

## Validation

The README program passed `claw check`, native build, execution with exit 0, and printed `Hello, Cat!` followed by `42`. Relative links were checked against the publishable repository tree and Markdown anchors. A clean Linux Release Clang build and the complete test runner passed. Windows execution is left to the repository CI matrix, not claimed as a local run.

## Boundary

Fast, Safe, Simple remain project standards, not completed performance/safety certifications. Arena stays pending/experimental; the PR #21 Str/String split is not described as available on main. No compiler or canonical PRD files changed.
