# Contribution guide (2026-10-17)

**Status:** documentation-only addition at implementation baseline `6b17b68`.

## Changes

Added [CONTRIBUTING.md](../CONTRIBUTING.md) describing design authority, focused vertical implementation slices, normal branch/PR workflow, explicit authorization for direct-main exceptions, build/test commands, intentional artifacts, review evidence, and a separate changelog file for each individual repository change. Windows guidance selects UCRT64 explicitly and avoids requiring global Git configuration changes.

## Validation

Cross-checked commands and requirements against AGENTS.md, CMake, CI, and test scripts. Checked relative links and anchors. The clean Linux Release build and complete repository test runner passed. Windows commands were reviewed against CI but not executed locally.

## Boundary

This does not make direct-main edits the normal contribution path or grant authorization for future commits. No compiler, PRD acceptance, or test implementation changed.
