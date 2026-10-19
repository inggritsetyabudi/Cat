# Windows PTY terminal contract

## Problem

On clean main `f3e9f54b7c5217d90622bea6093b35a2248bee70` and PR #29 head `12ab6f950e0f316560036caaa23f9295b2ad5967`, the Windows MSYS2 runtime writes `ESC[6n` while checking whether a newly opened PTY supports native console operation. A native control program whose `main` only returns zero produces the same bytes; pipe capture is empty. This is terminal startup traffic, not a compiler diagnostic or a regression introduced by PR #29.

The previous comment test waited for the child to finish before reading the master. It could not answer the cursor-position request. Native `_isatty(stderr)` was false after the runtime timed out, so the test was not exercising a genuine Windows console either.

Evidence: [exact baseline/control diagnosis](https://github.com/inggritsetyabudi/Cat/actions/runs/39184530805), [fixture, real-console preflight, and downstream stages](https://github.com/inggritsetyabudi/Cat/actions/runs/39184964022). The runtime implementation is [`term_has_pcon_cap`](https://github.com/msys2/msys2-runtime/blob/e13d9bd87314dba0c8ceaf8db65b131612914c6d/winsup/cygwin/fhandler/pty.cc#L4027-L4096).

## Change

Initialize a persistent native console keeper and service its startup protocol before launching the measured command. Require both native `_isatty(stderr)` and `GetConsoleMode` to succeed. Retain startup bytes separately, then capture every byte of the measured command without stripping or classifying escapes. Use bounded waits and clean up the measured child, keeper, PTY, and pump on failure.

The original comment-test predicate remains unchanged: exit 1, expected lexer header, and no ESC byte anywhere in the measured output. POSIX capture behavior is retained. New regression coverage checks a real native console, silent-child isolation, exact plain/SGR/CSI-6n byte preservation, nonzero exit status, and the actual compiler fixture. The terminal gate runs in the full suite before the comment gate.

No compiler, driver argv, lexer semantics, language design, version, tag, or release changes are included. PR #29 remains independent. Main's existing Windows path-with-spaces failure still needs PR #29; the dedicated integration job validates the PTY-only test overlay on that exact driver candidate without including driver changes in this branch.
