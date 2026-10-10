# Modularize Choice variant resolution

- Moved case-insensitive Choice variant-name lookup, generic payload substitution, and `Result`-like Choice recognition from `sema.cpp` into the stateless `choice_utils` module.
- Kept analyzer control flow, semantic diagnostics, and check ordering unchanged.
- Added a Cat end-to-end fixture covering a generic payload and a differently-cased constructor spelling, with AIR/OIR golden checks and native execution coverage.
