# Testing workflow guide (2026-10-17)

**Status:** documentation-only addition at implementation baseline `6b17b68`.

## Changes

Added [docs/TESTING.md](../docs/TESTING.md) with CI-equivalent Linux/Windows builds, explicit compiler selection, the seven runner stages, individual suite commands, generated artifact paths, failure reporting, and regression guidance. It preserves intentional backend/native artifacts and distinguishes local reproduction from platform certification.

## Validation

Executed the clean Linux Release Clang configure/build and `CLAW_EXE=... bash claw/tests/run_all_tests.sh`; all runner stages passed. Canonical PRD doc-tests classified 17 fences: one accepted example, one expected rejection, 13 proposals skipped, and two non-program text blocks skipped. Documentation-command, frontend, OIR, backend, native/sanitizer, and CLI suites also completed successfully. Checked links and anchors. No local Windows test run is claimed.

## Boundary

No test runner, assertion, fixture, compiler behavior, PRD acceptance marker, or artifact-retention behavior changed.
