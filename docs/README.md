# Cat documentation

Cat (C@) is a systems language in development, built around **Fast, Safe, Simple**. Claw is the compiler. This documentation separates what you can use on the current implementation baseline from what the approved design intends to provide.

## Start here

If you are new to Cat, read the [project overview](../README.md), [build Claw](BUILDING.md), and try the complete first program. Then use the [language reference](../SPEC.md) for current rules and the [CLI reference](CLI.md) to check, inspect, and compile your code.

| Goal | Guide |
| --- | --- |
| Understand the project and run a first program | [README](../README.md) |
| Read current syntax, semantics, and restrictions | [SPEC](../SPEC.md) |
| Install a toolchain and compile the compiler | [Building](BUILDING.md) |
| Use commands, output files, and `claw doc` | [CLI reference](CLI.md) |
| See what is implemented and what remains partial | [Implementation status](STATUS.md) |
| Understand compiler layers, ABI, and runtime | [Architecture](../ARCHITECTURE.md) |
| Reproduce tests and inspect artifacts | [Testing](TESTING.md) |
| Contribute a focused, evidence-backed change | [Contributing](../CONTRIBUTING.md) |

## Design versus implementation

The approved design lives in [cat-northstar/](../cat-northstar/README.md):

1. [PRD.md](../cat-northstar/PRD.md) defines language decisions.
2. [PRD-CHANGES-v0.1-to-v1.0.md](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md) defines pre-v1 staging and acceptance evidence.
3. [ROADMAP-v1-plus.md](../cat-northstar/ROADMAP-v1-plus.md) defines post-v1 sequencing and deliberately deferred work.

Those sources retain their authority and acceptance markers. This guide does not amend them. Compiler code, tests, and dated [changelogs](../changelog/README.md) determine implementation status. A ratified design, a milestone label, or a semantically registered method is not proof of end-to-end native support.

The former `revise/` summaries are preserved in [cat-northstar/legacy/](../cat-northstar/legacy/). The [ideas archive](../ideas/README.md) and legacy summaries provide historical context; conflicting old syntax or completion labels must not override the current approved design or compiler evidence.

## Existing compiler notes

The repository's specialist documents remain useful and are not replaced or deleted by this guide:

- [Claw overview](../claw/README.md) and [compiler notes index](../claw/docs/README.md).
- [Builtin method implementation audit](../claw/docs/builtin-methods.md).
- [Frontend safety status](../claw/docs/frontend-safety-status.md).
- [Compiler next-wave notes](../claw/docs/roadmap-next.md).
- [Repository/agent rules](../AGENTS.md).

Some older compiler notes describe the tested subset or toolchain as of their own update. Use the current [workflow](../.github/workflows/ci.yml), compiler driver, and test assertions to resolve implementation questions; use the canonical design documents to resolve intended language direction.

## Examples and documentation checks

Complete `cat` fences in the new public guides are intended to pass `claw check`; where a program has an entry point and uses the native subset, also build and execute it. Inline snippets illustrate syntax and are not standalone programs. Shell paths such as `/path/to/hello.cat` are placeholders to replace locally.

The existing automated PRD doc-test runner targets the canonical PRD and distinguishes accepted examples, expected errors, and skipped proposals. It does not automatically scan every public guide. See [Testing](TESTING.md) for the exact runner boundaries, and validate new public examples separately when editing them.
