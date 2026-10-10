# Changelog: Operator Overloading & Contract Interfaces

## Completed:
- Parser updated to parse `contract Name { ... }` and `implements Shape with ContractName { ... }`.
- Validation checks in Sema mapping implicit `self` matching parameter signatures. Added complete replacement and validation for `Self` keywords.
- Implemented constraint checks rejecting mismatch receiver (e.g. `ref mut` versus `ref`) and failing incomplete missing interfaces.
- OIR updated to inject `OirCallInst` during binary AST lowering for specific mapped operators:
  - `+` to `add`
  - `-` to `sub`
  - `*` to `mul`
  - `/` to `div`
  - `==`, `!=` to `equal`
  - `<`, `>`, `<=`, `>=` to `compare`
- Implemented and cleared positive native compilation scenarios:
  - Valid return from generic Self contract methods (`v1.clone()`).
  - Executing chained operations such as `v6 = (v1 + v2) + v5`.
- Updated test runner scripts and native test assertion output rules. All test suites pass 100%.

> Notes: Generic bound evaluation (`[T with Contract]`) was selectively skipped due to broad generic scope changes, which aligns with deferred expansion. Basic operator mappings function gracefully for all Shape structs locally defined.
