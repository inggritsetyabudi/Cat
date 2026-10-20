# 2026-10-20 — TEST ONLY: immutable Windows first-chance stack oracle prototype

Add an isolated Windows evidence experiment, not a replacement for the existing
backend stack diagnostic. PR #36 stays draft, its LLVM C++ adapter stays default
OFF, and no merge, production compiler/adapter change, or readiness claim follows.
The original workflow and terminal-status assertions are unchanged.

## What this experiment actually tests

The workflow builds a passive Win32 debug-event observer and two negative
controls on two Windows UCRT64 workers. It re-executes the four **immutable
attempt-1 compiler-generated binaries**, not newly rebuilt production compilers.
The archive is artifact `12238612261` from run `39364055321`, attempt 1, exact
source head `6d697e88a8f679e26732f906f85ae831cdc52bca`; the baseline source was
`28d5e3b2ea554a3a663561fc606c4bae78497540`. Archive and individual executable
SHA256 values are pinned, metadata is checked, and the original failed report
is retained. The current `claw` tree and original stack workflow must remain
identical to that source snapshot. Source drift fails closed instead of silently
claiming this historical binary validates a new compiler.

For baseline O0 the contract requires the exact `___chkstk_ms` instruction at
RVA `0x3cc1`, PE-backed instruction bytes, and a saved return into a verified
allocation callsite. It explores all 16 paths returning through the pinned final
Int16 loop backedge: each executes two 16-byte allocations with a net 32-byte
stack descent and no restoration of those allocations. Branch targets are
validated against PE instruction displacements, not labels alone.

The runtime classifier also requires first-chance `STATUS_STACK_OVERFLOW`, the
initial thread/TIB/reservation, an initial guard region, almost 2 MiB of observed
stack descent in the same frame, the low exhausted reservation/commit boundary,
`RAX=16`, the exact faulting push address, and matching runtime instruction bytes.
A terminal access violation alone can never satisfy this oracle. A supported
SO-then-AV sequence remains explicitly `BLOCKING`, not a normal passing exit;
its secondary faults must be in the same exhausted low-stack reservation.

Both unrelated no-access-memory AV and software `RaiseException(0xC00000FD)`
controls use the **same classifier**, without case-label rejection. They must
retain the exact native/debug terminal code and first/second-chance sequence.
They are also rejected when replayed through the classifier with the baseline
hash supplied deliberately, so hash mismatch alone cannot explain rejection.
Authentic baseline telemetry with a wrong hash must be rejected. Baseline O2
and fixed O0/O2 must exit zero in paired launches, match the independent fixture
bytes exactly, and have empty stderr. All four original PE stack reserve/commit
settings must equal 2 MiB / 4 KiB.

## Event handling and launch limits

Every raw debug event and continuation decision is retained. All application
exceptions are continued with `DBG_EXCEPTION_NOT_HANDLED`; only the initial,
validated ntdll loader breakpoint is continued as handled, as required to start
a normally debugged Windows process. There are no injected breakpoints, target
writes, recovery handlers, context edits, or guard-page reads. EXIT_PROCESS
must match `GetExitCodeProcess` after the process is signaled.

The native launcher is the original archived probe. Both helper invocations use
MSYS Python subprocess capture with no target arguments. The observer matches
its quoted executable command, NULL application/environment/current-directory
arguments, inherited stdin, inherited handles, error mode, and stdout/stderr
file sharing. The only intentional CreateProcess flag difference is
`DEBUG_ONLY_THIS_PROCESS`. Requested and actual image paths/process identity
are checked. Numeric handles, address layout, debug flags/BeingDebugged,
loader-breakpoint processing, suspended event delivery, helper bookkeeping,
heap/debug behavior, and exception dispatch/timing cannot be made identical.
Paired terminal equality is recorded, **not proof of debugger timing equivalence**.

## Initial bounded evidence, not closure of attempt 1

[Prototype run 39373001100](https://github.com/inggritsetyabudi/Cat/actions/runs/39373001100)
passed on both workers: 32 baseline O0 verified overflow sequences, 24 normal
variant paired runs, and 16 rejected negative-control paired runs, with 72 raw
event logs retained. Native and debug terminal statuses matched in these runs.
Both workers produced terminal `0xC00000FD`; the SO-then-terminal-AV branch was
not exercised by this new observer in that run. Earlier terminal `0xC0000005`
evidence and failed attempt 1 remain real, preserved, and unresolved by this
experiment. A successful later rerun never erases that failure.

The additive test-only commit requires its own exact-head Windows CI and review.
The workflow is limited to the dedicated diagnostic branch and PR36's existing
branch; it does not run on every unrelated PR. It does not weaken, relabel, or
replace any existing test or enable an experimental backend.
