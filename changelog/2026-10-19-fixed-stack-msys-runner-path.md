# Preserve MSYS executable paths in the fixed-stack regression runner

Exact candidate `bc2a0a59d6b8bf7d8b86d3a4a12d09c1d88af34b` passed clean local full Release/host ASan/UBSan/LSan suites, independent build/run review, both Linux CI jobs, and the Windows baseline/fixed native diagnostic. Windows now supplies direct causal evidence: immutable main's `-O0` executable exits with raw `0xC00000FD` (`STATUS_STACK_OVERFLOW`); baseline `-O2` and fixed `-O0`/`-O2` exit 0 with exact expected stdout and empty stderr. All four PE headers retain reserve 2,097,152 bytes and commit 4,096 bytes.

The complete Windows suites then stopped at the newly added Python regression runner because it converted the compiler executable to `D:\...` before asking MSYS Python's POSIX `execvp` to launch it. This is distinct from file arguments consumed by native Clang/Win32 APIs, which do require Windows spelling.

Separate executable-path conversion from native file-argument conversion. MSYS/Cygwin Python receives POSIX executable paths (`cygpath -u`), native Windows Python receives native executable paths, and native tool file arguments keep `cygpath -w`. Apply this only to compiler/native program launches in the new regression runner. Do not change fixture programs, exact stdout/exit/alloca assertions, stack settings, repetition, optimizer flags, compiler/backend, runtime, or existing tests.

Full exact-head validation and review are repeated after this harness-only correction. No tests are skipped or weakened and the PR remains draft pending coordinator review.
