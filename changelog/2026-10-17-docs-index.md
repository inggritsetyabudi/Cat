# Public documentation index (2026-10-17)

**Status:** documentation-only addition at implementation baseline `6b17b68`.

## Changes

Added [docs/README.md](../docs/README.md) as a task-oriented navigation page for the language reference, build/CLI/testing guides, architecture, contribution process, and implementation status. It explains the authority of the Northstar documents, the historical role of the former `revise/` summaries now in `cat-northstar/legacy/`, and the distinct role of existing compiler notes.

## Validation

Checked relative links and target Markdown anchors against tracked and newly added documentation paths. The complete Linux Release test runner passed, including canonical PRD doc-tests. The index explicitly notes that the existing PRD runner does not automatically scan every public guide; new public examples were checked separately.

## Boundary

No source, design decision, acceptance marker, archived document, or test runner was modified.
