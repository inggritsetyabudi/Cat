# Builtin Method Dispatch

Builtin method dispatch is static and compile-time only; there is no hidden dynamic dispatch. For string range views, PRD §14 is the contract authority; §31 remains an API inventory, not evidence of end-to-end implementation. A method being listed in the PRD or registered by semantic analysis does **not** by itself mean LLVM lowering or native execution is supported.

## End-to-End Verified Slice

The following `Str` methods are exercised by Cat fixtures through semantic analysis, LLVM IR verification, and native execution:

- `len() -> USize`
- `is_empty() -> Bool`
- `starts_with(prefix: ref Str) -> Bool`
- `ends_with(suffix: ref Str) -> Bool`
- `contains(part: ref Str) -> Bool`
- `text[a..b] -> ref Str` and `text[a..=b] -> ref Str` for `Range[Int32]`
- `text.get(a..b) -> Maybe[ref Str]` and the inclusive `Range[Int32]` form

String indices and range endpoints count **bytes** in the UTF-8 representation, not Unicode scalar values or grapheme clusters. The normalized half-open view must begin and end at UTF-8 boundaries: the start is `a`; the effective exclusive end is `b` for `..` and `b + 1` for `..=`. Thus an inclusive range's raw `b` may be a continuation byte when it names the final byte of a complete scalar (for example, the range covering both bytes of `é`). `..` excludes its end; `..=` includes it. Inclusive endpoints are widened to `i64` before adding one, so `Int32` maximum does not wrap; the effective exclusive endpoint must still be within the byte length. Direct range indexing defects on negative, reversed, out-of-bounds, or non-boundary effective endpoints; `get` returns `None` for those ranges and never defects. Valid ranges return a borrowed view from the receiver. These checks do not perform Unicode normalization, case folding, or whole-string UTF-8 validation.

The search methods use exact byte matching over the UTF-8 representation. Empty prefix, suffix, or search value matches; longer prefixes/suffixes and absent values do not match. This is not Unicode normalization, case folding, or grapheme-aware matching. Tests include an exact multi-byte UTF-8 search value and calls through borrowed `ref Str` parameters.

## Implementation Audit

| Receiver/methods | Semantic registration | LLVM/native status |
|---|---|---|
| `Str.len`, `Str.is_empty` | Registered | Existing native fixture covers them |
| `Str.starts_with`, `Str.ends_with`, `Str.contains` | Registered | End-to-end verified in the current dispatch slice; lowered to bounded native runtime helpers |
| `Str.byte_at`, `Str.first_byte`, `Str.last_byte` | Registered | LLVM lowering exists; not included in the verified search-method slice |
| `Str.find_byte`, `Str.count_byte`, `Str.contains_byte` | Registered | No LLVM builtin lowering yet |
| `Str[Range[Int32]]`, `Str.get(Range[Int32])` | Registered/typed as borrowed `ref Str` and `Maybe[ref Str]` | End-to-end LLVM/native coverage for exclusive, inclusive, empty, byte-boundary, invalid-safe, and defecting direct ranges |
| `Str.slice(start, length)` | Removed from public method dispatch; `check` rejects it | No public lowering path; use range indexing or `get(range)` |
| `Span.len`, `Span.is_empty` | Registered | LLVM lowering branches exist; native construction/coverage of `Span` is not established by current fixtures |
| `Span.slice(start, length)` | Registered | No `Span.slice` LLVM lowering yet; semantic acceptance must not be read as backend support |
| `Vec.len`, `Vec.is_empty`, `Vec.capacity` | Registered | LLVM lowering branches exist; broader collection coverage is incomplete |
| Other `Vec` methods registered in semantic analysis (`has_capacity`, `reserve`, `shrink_to_fit`, `clear`) | Registered | Not implemented end-to-end in LLVM/native |
| `Vec.truncate(USize)` and `Queue.truncate(USize)` | Not registered; `check` rejects these calls | No complete native implementation or defined element-destruction contract; `Queue` storage/codegen is also absent |
| Other `Map`, `Set`, and `Queue` methods listed by the catalog | Partially registered | Not implemented end-to-end in LLVM/native |
| Other PRD section 31 APIs (for example `Vec.push/pop/map/filter`, string transforms, and map lookup/update) | PRD inventory only where absent from semantic registration | Planned; not claimed as supported |

`Anchor.get()` and `Vec/Array.span()` have dedicated semantic paths, but their presence in semantic analysis is not evidence that every source form is natively constructible or lowerable. Borrowing from `Vec` elements still requires a `Span`; mutating a `Vec` while a span is live remains rejected. The canonical PRD fixes the `Vec` representation but does not define `truncate` behavior, particularly destruction of removed owned elements, so `Vec.truncate` and `Queue.truncate` remain unavailable rather than being accepted and failing during LLVM lowering.

## PRD Difference Requiring Design Reconciliation

PRD §14 defines range indexing as a receiver-borrowed `ref Str`, and `get(range)` as `Maybe[ref Str]`; this implementation adds that focused `Range[Int32]` vertical slice. The legacy public `Str.slice(start, length)` dispatch is removed rather than treated as a substitute. The older slice entry in §31 remains an inventory discrepancy for future reconciliation. This change does not modify PRD status and does **not** claim that all of §14 is complete.

## Dispatch Principles

- Methods resolve from the receiver type in semantic analysis.
- Receiver borrowing and view-return provenance remain explicit in ownership analysis.
- Add methods in vertical slices: semantic validity, ownership/borrowing, IR/LLVM lowering, native execution, and positive/negative tests.
- Keep unverified entries marked as planned or incomplete; do not equate semantic registration with end-to-end support.
