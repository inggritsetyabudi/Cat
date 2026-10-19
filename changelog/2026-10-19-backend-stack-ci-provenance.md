# Exact-head Windows backend validation plumbing

The first backend-stack candidate (`a2369106350aeaf8b975e81583e49a7e0850f239`) passed both Linux CI jobs, clean local full Release/host ASan/UBSan/LSan suites, and independent exact-SHA build/run review. Its Windows CI jobs stopped in validation infrastructure, not backend execution:

- The main workflow's new provenance command inherited `msys2 {0}` before MSYS2 setup had run, so the shell itself was unavailable. Run that pre-setup `git rev-parse HEAD` using PowerShell, where checkout's Git is available.
- The new Windows baseline/fixed diagnostic records Git provenance inside UCRT64, so install MSYS2 `git` explicitly alongside the existing toolchain packages.
- The historical PTY workflow overlaid a small hardcoded file list (including the evolving `run_all_tests.sh`) onto immutable PR29 compiler code. The newly referenced regression runner was not included; adding that file alone would still test the wrong compiler baseline. Replace the incomplete historical overlay with a clean exact-PR-head compiler and test checkout. Keep the complete fail-fast suite, including terminal transport, lexer, native and new backend regression gates. No suite, assertion or test invocation is removed or conditionally skipped.

These are CI-only corrections. The backend lowering, runtime, test programs/oracles and UI/CLI remain unchanged from the independently reviewed candidate. Final exact-head CI and revalidation evidence is recorded in the draft PR; no version/tag/release, PRD marker, Arena or borrowed-field changes are made.
