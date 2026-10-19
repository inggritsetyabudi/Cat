# 2026-10-19 — Compact native build progress and diagnostics

## Requested contract

This workstream replaces the native build's Forge banner, legend, plan, and spinner with compact progress. Stage names are `Resolving`, `Checking`, `Ownership`, `Lowering`, `Emitting`, and `Linking`. Build status and diagnostics use stderr; successful builds leave stdout empty. Other commands retain their existing output and stage-qualified diagnostics.

Plain `claw build` selects native `clang -O0`; `claw build --release` selects `-O2`. Successful output follows the requested wording:

```text
Finished native release [debug + unoptimized] in 0.123s
     Output /absolute/path/to/program.exe
```

The release profile substitutes `[optimized]`. The literal `native release` wording is retained for both profiles as requested; the bracketed profile identifies optimization. Elapsed time covers the build, not just Clang invocation.

## Engineering boundaries

Progress follows actual driver and backend work. Resolution includes package/module loading; semantic and ownership checks identify the unit being checked. The total counts compiler work tasks, not source lines, estimated time, or a fabricated fixed 30. The module count becomes known only after resolution. OIR/LIR construction is separated from LLVM emission before native linking.

TTY output reuses one compact line. Redirected stderr records newline-separated events with no ANSI or carriage returns. Supported POSIX terminals use cyan bars, dim counters/module names, bold stages, yellow/red warning/error headers, green `Finished`, and cyan `Output`. `NO_COLOR`, `TERM=dumb`, and the Windows no-ANSI fallback are preserved. Warnings are printed when discovered; compilation continues only where safe.

Build diagnostics show simple severity headers and source locations/snippets. Optional primary and secondary labels identify the actual move and borrow origin where available. Origin spans travel with active borrow tokens through state snapshots and do not participate in borrow identity. Unknown origins are not fabricated. Existing diagnostic messages and hints remain; unrelated command formatting is unchanged. Tab prefixes are preserved and build-only carets are clipped to the actual source line.

Failures retain the underlying diagnostic or native-launch/link detail and end with `error: build failed for` and the project/input name. Native argument handling, compiler exit status, retained LLVM artifacts, and temporary stderr-file hardening remain covered by regressions.

## Validation and provenance

The live baseline was verified as `28d5e3b2ea554a3a663561fc606c4bae78497540`; its post-merge CI passed. A clean disposable Release clone reproduced the old verbose success and parse-error output before changes. The new contract tests reject that baseline output.

Regression coverage includes real single-file and workspace/module native builds, both optimization selections, warning continuation, error snippets and borrow/move labels, output paths with spaces, stdout/stderr separation, terminal color policy, and existing native-process/security cases. Formatter tests cover tabs, clipping, missing source, cross-file secondary notes, and preservation of legacy formatting. Legacy rejection matrices distinguish compact build source errors and the final named failure from stage-qualified non-build headers. Inline rejection tests compare the full existing semantic message list across commands, including cases that legitimately emit more than one source error.

The Windows PTY workflow previously combined an old fixed PR29 compiler with current tests. It now checks out the exact PR head for both compiler and tests. Linux Release, Linux host ASan/UBSan/LSan, and Windows UCRT64 CI also check out the exact PR head. Validation results and independent review are recorded against the final candidate SHA in the draft PR; workflow existence alone is not passing evidence.

## Explicit residuals

These flags select native optimization only. They do not implement pending PRD debug overflow defects, complete debug metadata/symbols, or other language-level debug/release semantics. No `-g` promise is made. Clang diagnostics remain genuine toolchain output and are not silently suppressed or restyled. On POSIX the hardened temporary descriptor still captures Clang stderr and replays it after the Clang process exits; internal Cat diagnostics are immediate, but live subprocess-stderr streaming is a residual. A blocking source error stops unsafe later stages; progress continuation does not mean compiling invalid code.

This work does not change version, tag, release, language semantics, or PRD/G2 acceptance labels. It is a draft-review workstream, not authorization to merge.

## Main integration and PTY measurement boundary
Main `f1b74544f32b989d1c5c016fb552dd972edad9e0` was merged into the existing UI branch without rebasing or rewriting published commits. Backend fixed-stack changes remain identical to main; the UI workstream does not modify them.

The CLI status capture now uses the established native console keeper/barrier on MSYS/Cygwin before measuring the child. Startup protocol bytes are retained separately, validated fail-closed, and never filtered from measured output. An optional stdout sink preserves the compiler stdout/stderr separation. Existing no-ANSI, palette, error, and status assertions are unchanged. POSIX capture is unchanged.

The inherited Windows baseline-stack diagnostic oracle remains `0xC00000FD`. A run observing a different raw baseline exception is recorded as a failed diagnostic, not relaxed into a pass; independent comparison evidence is retained separately from UI acceptance. All updated-HEAD full-suite and review results must be read from the PR, not inferred from earlier SHA results.

The console keeper alone initializes with a capable terminal environment; the measured child retains the original environment exactly, including `TERM=dumb` and `NO_COLOR`. This keeps the pre-measurement transport setup separate from the compiler color-policy scenario under test.

## Host-native module target alignment

The existing target mapping now has one source of truth shared by LLVM emission and native invocation. For the existing GNU/Linux x86_64/aarch64 and Windows x64 host branches, Clang receives `--target=` matching the module triple when compiling both IR and runtime C. Plain builds retain `-O0`; release builds retain `-O2`. No vendor aliases are assumed equivalent, no warning flags are disabled, and stderr forwarding is unchanged. Apple deployment-target and unsupported/fallback-host driver policies are intentionally unchanged rather than inventing new target support.

The new regression rejects the previous successful Linux build's `override-module` diagnostic, verifies exact target/IR agreement and optimization argv, and executes single-file and workspace debug/release outputs. A copied compiler/runtime fixture generates a genuine Clang `#warning` in both profiles, verifies it remains visible without an override warning, and executes the resulting programs. Existing missing-Clang/process-error, terminal, ownership, and failure assertions remain intact. Full clean-suite, exact-head CI, and independent-review results are tracked per candidate SHA in the draft PR; targeted success is not a substitute for those gates.
