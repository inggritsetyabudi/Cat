# 2026-10-20 — Exact singleton production evidence gate, no aggregate adapter lowering

Add one narrow production-native probe for the exact internal Cat round-trip:

```cat
fn make(x: Int32) -> (Int32,) { (x,) }
fn take(p: (Int32,)) -> Int32 { p.0 }
fn main() -> Int32 { val p = make(42) return take(p) }
```

The opt-in CTest gate runs the normal production compiler through `check`, OIR,
LIR, LLVM IR and native O0/O2 builds. Both executions must exit 42 with empty
stdout and stderr. It checks internal Cat `abi=claw`, aggregate Direct parameter
and result signatures, nonpacked `{ i32 }` storage, field-0 initialization before
whole-value use, projection and whole-value transfers. This is not a C ABI/FFI
classification or a promise that tuple ABI is stable for external code.

A test-only helper compares the actual frontend tuple layout with LLVM's native
TargetMachine DataLayout and the effective native Clang module DataLayout. It
checks aggregate kind, Direct passing, `ffiStable=false`, one Int32 field named
`0`, size, alignment and offset rather than treating a host C struct as Cat ABI
proof. Production IR currently omits DataLayout; both the original and effective
Clang IR and computed layout evidence are retained explicitly.

A separate fail-closed gate requires the current adapter to reject the singleton
with default flags, local binding keys OFF/ON, and all existing feature flags
in both identity modes. There is no tuple-lowering or tuple-provenance CLI flag;
none is invented here. The exact unsupported-tuple diagnostic is required so a
CLI usage error cannot satisfy this gate. Both absent
outputs and pre-existing object/IR sentinel files are checked, so rejection cannot
be mistaken for a newly published artifact or overwrite stale evidence. All prior
corpora and gates remain in place. Linux release/sanitizer and Windows native
UCRT64 CI upload the probe's command/results, IR, layout and executable evidence.

This change does **not** implement aggregate lowering, add a generic field store,
modify production codegen/semantics, enable any backend by default, establish C
ABI compatibility, or close malformed-LIR/invalid-slot/store/field/initialization
acceptance gates. The helper inspects only canonical production-generated LIR;
rejection of a whole unsupported tuple does not validate an eventual aggregate
lowerer. Native Windows results must actually pass before the exact-target
evidence gate can be called closed. Windows sanitizer remains unproven/SKIP.
PR #36 remains draft and default OFF; there is no merge, version bump, PRD marker,
or v0.1–v0.3 maturity/readiness claim.
