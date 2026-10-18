# Implementation status

Cat is under active development. This page describes `main` at implementation baseline **`e7789c6efedff677b81c2f883fe8498e32c30554`**, not a tagged release, completed milestone, stable ABI, or production certification. Documentation-only updates do not advance that implementation baseline.

The guiding standards are **Fast, Safe, Simple**. The [approved PRD](../cat-northstar/PRD.md) defines design; compiler code, tests, and dated [changelogs](../changelog/README.md) establish implementation. Frontend acceptance, LLVM lowering, and native execution are different levels of evidence.

The [G2 feature inventory](../cat-northstar/FEATURE-STATUS.md) is the canonical per-slice label ledger. This overview is explanatory, not a second status inventory. G2 and the v0.1–v0.3 maturity gates remain open pending the residuals recorded there; accepted compiler slices are neither bug-free nor production certification.

## Available slices and their limits

| Area | Present on this baseline | Important limit / evidence |
| --- | --- | --- |
| Core syntax | Revised bindings, functions, borrowing, control flow, `shape`, `choice`, `Result`, `Maybe`, `try`, and `pick` | Individual semantic/backend restrictions remain; see [SPEC.md](../SPEC.md). |
| Modules and entry | File-based modules, `share`, explicit/aliased imports, cycle and privacy checks, root `main.cat` | Entry is not importable or callable; broad imported-program backend coverage is not claimed. [Module evidence](../changelog/2026-10-11-v0.1-module-path-namespace.md). |
| Ownership | Move/initialization checks, path-based field overlap, lexical borrow checks, cleanup scheduling | Not the completed CFG/liveness-based dataflow checker targeted by v0.7, nor a Rust-level memory-safety guarantee. [Safety notes](../claw/docs/frontend-safety-status.md). |
| Scoped values | Tested `scope`, `ref[s]`, local view shapes, direct/nested generic scoped propagation | Expected concrete types are needed for nested generic shape literals; no general lifetime inference. [Nested scoped types](../changelog/2026-10-09.Nested-Scoped-Generics.md). |
| Stable ownership | Bounded `Anchor.new` / `get` and tested cleanup | Arbitrary nested owned payloads are not supported. [Nested payload restriction](../changelog/2026-10-12-anchor-nested-payload-safety.md). |
| Tuples | Plain tuples, projections, nested destructuring and tuple `pick` patterns; selected owned fields | Not arbitrary owned aggregate cleanup. [Tuple cleanup](../changelog/2026-10-11-v0.3-owned-tuple-cleanup-partial.md) and [nested destructuring](../changelog/2026-10-14-v0.3-nested-tuple-destructuring.md). |
| Fixed arrays | Tested `Array[Int32, N]` and `Array[UInt8, N]` construction, indexing, scan, and local mutable indexed stores | Compound, global, nested, and broader element-type paths are not general support. [Indexed assignment slice](../changelog/2026-10-16-v0.3-fixed-array-index-assignment-partial.md). |
| Ranges | Integer-width and Unicode `Char` construction/membership/scan in tested forms | String views remain `Range[Int32]`; this is not universal collection slicing. [Wide ranges](../changelog/2026-10-14-int128-uint128-ranges-partial.md), [Char ranges](../changelog/2026-10-12-char-ranges-partial.md). |
| Strings | Length-aware UTF-8 data, selected receiver methods, byte-based range views, safe `get(range)` | Not grapheme indexing, normalization, or complete string-library support. [Builtin audit](../claw/docs/builtin-methods.md). |
| Literals | Integer radix/separators/suffixes, Unicode scalar `Char`, ordinary string escapes including NUL and Unicode | No completed f-string/multiline-string milestone. [Integer literals](../changelog/2026-10-12-integer-literals-v04.md), [string escapes](../changelog/2026-10-16-v0.4-string-escapes-partial.md). |
| Scalar type names | Closed builtin scalar list accepts ASCII case variants; aliases normalize to canonical types | User types, containers, generic parameters, functions, fields, and variants remain case-sensitive. [Casing slice](../changelog/2026-10-16-builtin-scalar-type-case-insensitivity.md). |
| Documentation | Top-level `///` data, non-fatal warnings, `claw doc <source.cat>` Markdown | Parse-only, one file, documented shared functions/shapes/choices; no member/import traversal. [Documentation slice](../changelog/2026-10-16-v0.4-doc-comments.md). |
| Native tools | LLVM text emission, host `clang -O2` builds, bundled C runtime, Forge status | One optimized native profile; not selectable debug/release language profiles or general cross-compilation. [CLI](CLI.md). |
| Platform regression gates | Release Clang builds and complete suite on Linux and Windows UCRT64 in CI | The existence of a workflow does not certify all targets or an unobserved run. [Workflow](../.github/workflows/ci.yml). |
| Span ABI | Win64 indirect by-value parameter handling for the tested Span paths | Not complete general FFI support. [ABI fix](../changelog/2026-10-16-win64-span-by-value-abi.md). |

## Not available as a complete feature

The full revised `Vec`/`Set`/`Map`/`Queue` literal and method surface remains incomplete. Some names and methods are semantically registered without native lowering. In particular, `Vec.truncate` and `Queue.truncate` are rejected rather than exposed with an undefined element-destruction contract. Use the [builtin implementation audit](../claw/docs/builtin-methods.md) instead of treating a catalog entry as an API guarantee.

Broad raw/FFI support, full operator/contract/annotation coverage, closures, general aggregate lowering, dataflow lifetime analysis, and the remaining release optimization/hardening gates must be evaluated against their own tests and acceptance criteria. Do not copy intended `foreign c` examples into claims of a complete current C interoperability layer.

**Arena remains pending/experimental.** Historical code or runtime support does not establish a complete supported Arena language contract. The official fixed-pool profile is staged after v1 in the [roadmap](../cat-northstar/ROADMAP-v1-plus.md). This documentation does not alter Arena code, design, or acceptance status.

The **borrowed `Str` / owned `String` foundation from PR #21 is on this main baseline**. Literal `Str` uses static borrowed UTF-8 storage; `String.from`, `as_str`, explicit clone/move and tested recursive cleanup are bounded by the [ownership changelog](../changelog/2026-10-17-str-string-ownership.md). Generic collection storage for `Str`/`String`, arbitrary owned projection/partial moves, interpolation and text-conversion contracts are not implied by that foundation.

PRs [#22](https://github.com/inggritsetyabudi/Cat/pull/22), [#23](https://github.com/inggritsetyabudi/Cat/pull/23), [#24](https://github.com/inggritsetyabudi/Cat/pull/24), [#25](https://github.com/inggritsetyabudi/Cat/pull/25), and [#26](https://github.com/inggritsetyabudi/Cat/pull/26) were open at the G2 baseline audit. Their partial-move protection, division/remainder edges, target-direct float literals, integration/oracle evidence, and Ringkas generator are not credited to main. The [acceptance matrix on PR #25](https://github.com/inggritsetyabudi/Cat/blob/2be9c492d8e2852aef48329c86b636ea8e266376/docs/v0.1-v0.3-acceptance-matrix.md) is a pinned review reference, not a file or acceptance result on main.

## Milestones are not release claims

The [v0.1–v1.0 change plan](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md) defines acceptance criteria, not a list this documentation can mark complete. A slice named “v0.3” or “v0.4” in a changelog does not establish completion of that milestone. A clean Release build does not establish language stability, the PRD's performance target against C, complete safety, or v1.0 readiness.

The [post-v1 roadmap](../cat-northstar/ROADMAP-v1-plus.md) stages tooling expansion, self-hosting, macros, threading/channels, embedded support, and later explorations. A small early implementation of `claw doc` does not complete the broader tooling roadmap. No delivery dates are promised here.

## How to verify a claim

Start with the relevant fixture and assertions in [frontend](../claw/tests/frontend/), [backend](../claw/tests/backend/), or [native](../claw/tests/native/). Run the documented [test workflow](TESTING.md) with an explicit compiler path. For a native claim, check both emitted IR and executable behavior; for a safety rule, include a rejection case. Preserve the tested boundary when updating documentation.
