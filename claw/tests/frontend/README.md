# Revised Frontend Tests

This folder now contains only revised-language frontend fixtures.
Fixtures live in `fixtures/`.

Recommended runner (from `claw/`):
- `bash tests/frontend/run_frontend_tests.sh`
- Override the compiler path if needed with `CLAW_EXE=/path/to/claw.exe bash tests/frontend/run_frontend_tests.sh`

Current fixtures:
- `revise_surface.cat`
  Locks in the revised surface basics: `val` / `var`, `ref`, `if` / `else`, file modules without `realm`, and generic `[]`.
- `revise_pkg/`
  A revised workspace fixture covering root `main.cat`, file-based modules in the `src` namespace, and item import aliases without a folder manifest.
- `revise_module_aliases/`
  Verifies namespace imports and required aliases when two module files share the same final path segment.
- `revise_super_import/`
  Verifies `import super.{ ... }` resolves a sibling module one directory level up.
- `revise_bad_module_cycle/`, `revise_bad_private_type_export/`, and `revise_bad_private_item_import/`
  Verify cycles are rejected, private types cannot escape through `share`, and unshared items cannot be imported.
- `revise_bad_super_directory/`
  Verifies `import super` rejects a directory whose name ends in `.cat` instead of treating it as a module file.
- `revise_name_case.cat` and `revise_bad_case_*.cat` / `revise_bad_case_*/`
  Verify capitalized type names, lowercase declarations and module names, and the closed built-in variant exceptions.
- `revise_bad_main_return/`
  Expected to fail validation because root `main` must take no parameters and return `Unit` implicitly or explicitly.
- `revise_error_handling.cat`
  Locks in `Result[T, E]`, `Ok(...)`, `Fail(...)`, `try`, and `try ... else ...`.
- `revise_try_error_alias/`, `revise_bad_try_alias_generic_mismatch/`, and `revise_bad_try_alias_cross_module/`
  Verify an imported nominal generic error type remains identical through a supported item alias, while different generic arguments and same-spelling types from distinct modules remain mismatches.
- `revise_bad_try_return.cat`
  Expected to fail because shorthand `try` requires the current function to return `Result[T, E]`.
- `revise_bad_try_var.cat`
  Expected to fail because `try` is only allowed in a `val` initializer.
- `revise_bad_try_return_expr.cat`, `revise_bad_try_error_type.cat`, and `revise_bad_try_else_nonterminal.cat`
  Lock in rejection of `return try`, shorthand error-type mismatch, and a nonterminal `else err` block.
- `revise_bad_try_owned_reuse.cat`
  Expected to fail because `try` consumes an owned `Result`; its source binding cannot be used again.
- `../native/fixtures/revise_try_contract.cat`
  Locks in AIR typing for exactly one Result layer, alongside native success/failure coverage.
- `../native/fixtures/revise_try_owned_result.cat`
  Locks in OIR drop behavior and native owned-`Result` propagation/temporary paths, including AddressSanitizer and LeakSanitizer checks when available.

Ownership-specific invalid reuse is asserted by `check` and OIR; the current AIR command emits after semantic analysis and does not run the ownership pass.

Coverage focus:
- revised parser surface
- revised module/workspace rules
- revised AIR printing
- revised `Result` / `try` semantic validation
- `Range[Char]` typing, membership, indexed/plain scans, and Unicode scalar boundaries
- canonical nominal identity through imported item aliases
- owned `Result` consumption and payload transfer
- Anchor stable-storage semantics and scoped/borrow diagnostics
- revised diagnostics for entry and `try` misuse

Backend and native integration suites live separately:
- `bash tests/backend/run_backend_tests.sh`
- `bash tests/native/run_native_tests.sh`
