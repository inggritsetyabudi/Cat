# Fixed-size LLVM storage is allocated once per function activation

## Defect and reproduction

The unmodified `revise_int16_range.cat` native fixture exhausts a Linux child stack limited to 1,048,576 bytes when the IR from main `28d5e3b2ea554a3a663561fc606c4bae78497540` is linked at `-O0`: native return status is SIGSEGV (`subprocess` returncode -11). The same program exits 0 with the exact expected 171-byte stdout at `-O2`, or at `-O0` with the host's normal 8 MiB stack. Main's native driver still selects `-O2`; the diagnostic invokes `claw build` with a test-only clang wrapper changing that argument to `-O0`, and also tests direct unmodified generated-IR relinking. No driver or UI behavior is changed here.

The range iterator state was already entry-allocated. The defect is fixed-size local and temporary `alloca` instructions emitted inside loop/control-flow blocks: LLVM retains these allocations until function return, so unoptimized native code repeatedly decreases the stack pointer. In the original fixture, synthetic Boolean bindings occur on the full Int16 range backedges. Linux disassembly confirms 16-byte stack-pointer decrements per repeated Boolean allocation; optimization merely hid the defect.

## Backend change

All remaining fixed-size allocations join the existing per-function `entryAllocas` collection: binding-ID locals, indirect call result buffers, choice construction and pattern-binding temporaries, address spills, string equality operands, and `try` propagation return buffers. Function body rendering is deferred until all blocks and helper-generated blocks have been lowered, then the completed allocation list is written before the original first-block instructions. Iterator and safe string-range storage retain the same mechanism.

Only storage placement changes. Initializers, calls, stores, choice tags, ownership drops, control-flow edges, binding maps, unique slot names, type layout and alignment stay at their original execution points. A slot remains distinct for every static allocation site; sibling/nested bindings and overlapping temporaries are not coalesced. Every recursive/re-entrant invocation has its own stack frame. The allocation lifetime is the function activation, not a heap-owned value's lifetime; existing per-scope destruction is unchanged. This uses the shared LLVM lowering path for textual LLVM and native code on Linux and Windows, not an optimizer, larger stack, or alternate execution mode.

The fixed original fixture's Linux x86-64 Clang 18 `-O0` generated main reserves 392 bytes once, with no loop-driven stack-pointer changes. This is a measured frame size for that program/toolchain, not a universal stack bound: static frame size and recursion depth still matter.

## Validation and scope

Targeted regressions require actual `check`, `llvm`, `build`, native execution at `-O0` and `-O2`, exact stdout/status, entry-only allocation structure and high-repetition execution under a real 1 MiB Linux child-stack limit. Additional coverage exercises nested and conditional constructs, composites, synthetic locals, temporaries, shadowed bindings and recursion. Windows diagnostics must record native status and PE stack reserve/commit; a shell suite exit 127 alone is not proof of a Windows stack overflow.

Final exact-SHA full Release, host ASan/UBSan/LSan, independent review and Linux/Windows CI results are recorded in the draft PR and workstream evidence. No tests are weakened. No CLI/UI, version, tag, release, PRD acceptance marker, Arena or borrowed-field semantics are changed. The PR remains draft pending coordinator review; nothing is merged automatically.
