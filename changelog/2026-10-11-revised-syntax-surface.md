# 2026-10-11 — Revised syntax surface

- Removed legacy spellings (`hold`, `slot`, `give`, `look`, `edit`, `when`, `otherwise`, `of`, and `realm`) from the lexer keyword set. They no longer have special parser behavior or compatibility aliases; grammar accepts the revised forms (`val`, `var`, `return`, `ref`, `ref mut`, `if`, `else`, and bracket generics).
- Removed the live `lift` statement, its AST node, semantic and ownership handling, and its dedicated lowering path. `try expr` and `try expr else err { ... }` remain the supported Result-handling syntax and backend route.
- Renamed internal return/conditional AST nodes and binding/try IR instructions to match the revised surface. Internal borrow-kind tags remain implementation-private and unchanged.
- Extended frontend regression coverage to assert that old spellings, including `lift`, do not compile.
- Arena behavior and module maintenance/exposure (including `modules.cat`) are intentionally unchanged and out of scope for this workstream.

Validation status is recorded after the implementation test run; no PRD feature status is promoted by this changelog.
