# Changelog: Minimal Arena Allocator

## Completed:
- Implemented `Arena.new()` construction logic in semantic checker as a scoped builtin allocation pool.
- Added compiler interception for `arena.alloc(value)` and `arena.reset()` methods.
- Integrated `Arena` returns with the ownership engine: functions attempting to return an allocated structure bound to a locally instantiated Arena will be safely rejected by the compiler (`Returned ref value must come from one of the function's ref parameters.`).
- Engineered a chunked bump-pointer allocator in `native_runtime.c` to serve `claw.runtime.arena.alloc`. Automatically aligns allocations to the required bytes of the payload.
- Injected `claw.runtime.arena.free` at scope drops to securely reclaim block lists.
- Covered full flow with a validated native execution test fixture (`revise_arena.cat`). All suites maintain a 100% pass rate.