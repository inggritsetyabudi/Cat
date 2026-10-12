# Modularize workspace manifest parsing

- Extract `modules.cat` parsing and its parser-only helpers from `workspace.cpp` into `workspace/manifest.cpp`.
- Keep the existing `ProjectLoader` cache, path normalization, diagnostics, accepted syntax, and import-resolution behavior unchanged.
- Register the new translation unit with the compiler target.
- This is a structural refactor only; no language semantics or Arena behavior is changed.
