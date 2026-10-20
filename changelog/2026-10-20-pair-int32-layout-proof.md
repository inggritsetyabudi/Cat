# Test-only Int32 pair layout proof

Base audited source: `c5d901643f50a212800916f5a8d084836b03d2b8` (PR #36 open/draft, ten successful checks before this change). This change adds only a dedicated test utility, its workflow, and immutable source-manifest controls. The compiler, runtime, adapter tree, production tests and existing workflows are unchanged.

`tools/pair_layout` builds only a fresh production compiler and a separate probe. The probe examines actual semantic `TypeLayoutInfo` for the existing production `make`/`take` fixture, then compares the literal nonpacked LLVM `{i32,i32}` under the observed effective native Clang DataLayout and an explicitly selected native LLVM 23.1.3 TargetMachine. The runner records actual raw/effective triples, signatures, field GEPs and command output. No missing raw DataLayout is replaced with a guessed value. Comparison concerns numeric observables, not equality of DataLayout strings or a claim about C ABI.

The dedicated workflow runs on native Linux x86_64 ELF and native Windows x64 MinGW UCRT COFF. It uses the existing checksum-pinned Linux SDK fetcher and the exact checksum-pinned official Windows LLVM 23.1.3 package. It does not invoke the full suites or build adapter/runtime/LLD targets. Other existing PR checks remain independent and unchanged.

Reproduce from a committed exact test head with native Clang and the exact SDK:

```sh
cmake -S tools/pair_layout -B build-pair -GNinja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DCLAW_LLVM_CONFIG=/path/to/exact23-sdk/bin/llvm-config
cmake --build build-pair --target claw-pair-layout-probe claw --parallel 2
python3 tools/pair_layout/run.py --compiler build-pair/claw/claw \
  --probe build-pair/claw-pair-layout-probe --clang clang \
  --expected-head "$(git rev-parse HEAD)" --artifacts /fresh/evidence/directory
```

On native Windows use UCRT64 compiler/Python/LLVM tools and `.exe` executable suffixes. A Linux run targeting Windows is not accepted as Windows evidence. Evidence must show PASS from each actual runner before claiming bounded numeric equivalence; a checked-in workflow is not execution evidence.

The immutable manifest continues to pin the adapter to reviewed `f6acc88` and the production pair fixture/harness blobs to reviewed `ceff0e9`. New layout utility/workflow/changelog blobs receive their own exact reviewed commit/object pins, not directory ignores or new maintenance exemptions. Controls reject drift, deletion, executable/symlink mode changes, and unknown additions.

Pair adapter admission stays CLOSED: no `--pair-i32`, no pair codegen, no malformed-LIR matrix or adapter object/link/native acceptance. Default OFF and existing singleton behavior are unchanged. Windows sanitizer remains SKIP, not PASS. Original attempt-1 Windows stack AV `0xC0000005` remains unresolved and retained. Arena and borrowed shape fields remain deferred. No merge, version or PRD marker.
