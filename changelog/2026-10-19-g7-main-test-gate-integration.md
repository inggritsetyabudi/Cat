# G7 integration with current main

PR #26 is integrated with main `f11e043e6a6f5aea1005f2dbb4332fa980285dba`, rather than the older, unfinished local integration worktree.

The conflict in `claw/tests/docs/run_doc_tests.sh` is resolved by retaining both sets of gates: the byte-exact G7 generated-summary check and generator regressions, followed by the G2 inventory validator, executable evidence checks, and all feature-status regressions. Executable PRD fence checking remains last. No compiler/runtime code or test gates from main are removed.

The 22 approved top-level `Ringkas:` markers and generator are unchanged. Current CI includes Linux Release, Linux ASan/UBSan/LSan, and Windows UCRT64 Release; final results are recorded with the exact reviewed head on PR #26, not predicted here.
