# Builtin Method Dispatch

Builtin method dispatch is static and compile-time only; there is no hidden dynamic dispatch. This document distinguishes the intended API in PRD section 31 from the subset verified through the compiler. A method being listed in the PRD or registered by semantic analysis does **not** by itself mean LLVM lowering or native execution is supported.

## End-to-End Verified Slice

The following `Str` methods are exercised by Cat fixtures through semantic analysis, LLVM IR verification, and native execution:

- `len() -> USize`
- `is_empty() -> Bool`
- `starts_with(prefix: ref Str) -> Bool`
- `ends_with(suffix: ref Str) -> Bool`
- `contains(part: ref Str) -> Bool`

The search methods use exact byte matching over the UTF-8 representation. Empty prefix, suffix, or search value matches; longer prefixes/suffixes and absent values do not match. This is not Unicode normalization, case folding, or grapheme-aware matching. Tests include an exact multi-byte UTF-8 search value and calls through borrowed `ref Str` parameters.

## Implementation Audit

| Receiver/methods | Semantic registration | LLVM/native status |
|---|---|---|
| `Str.len`, `Str.is_empty` | Registered | Existing native fixture covers them |
| `Str.starts_with`, `Str.ends_with`, `Str.contains` | Registered | End-to-end verified in the current dispatch slice; lowered to bounded native runtime helpers |
| `Str.byte_at`, `Str.first_byte`, `Str.last_byte` | Registered | LLVM lowering exists; not included in the verified search-method slice |
| `Str.find_byte`, `Str.count_byte`, `Str.contains_byte` | Registered | No LLVM builtin lowering yet |
| `Str.slice(start, length)` | Registered | LLVM path exists for `Str`; dedicated end-to-end coverage remains outstanding |
| `Span.len`, `Span.is_empty` | Registered | LLVM lowering branches exist; native construction/coverage of `Span` is not established by current fixtures |
| `Span.slice(start, length)` | Registered | No `Span.slice` LLVM lowering yet; semantic acceptance must not be read as backend support |
| `Vec.len`, `Vec.is_empty`, `Vec.capacity` | Registered | LLVM lowering branches exist; broader collection coverage is incomplete |
| Other `Vec` methods registered in semantic analysis (`has_capacity`, `reserve`, `truncate`, `shrink_to_fit`, `clear`) | Registered | Not implemented end-to-end in LLVM/native |
| `Map`, `Set`, `Queue` methods listed by the catalog | Partially registered | Not implemented end-to-end in LLVM/native |
| Other PRD section 31 APIs (for example `Vec.push/pop/map/filter`, string transforms, and map lookup/update) | PRD inventory only where absent from semantic registration | Planned; not claimed as supported |

`Anchor.get()` and `Vec/Array.span()` have dedicated semantic paths, but their presence in semantic analysis is not evidence that every source form is natively constructible or lowerable. Borrowing from `Vec` elements still requires a `Span`; mutating a `Vec` while a span is live remains rejected.

## PRD Difference Requiring Design Reconciliation

PRD section 31 describes `text.slice(range) -> Str`. The current semantic signature instead accepts two `USize` arguments (`start`, `length`) and returns a borrowed `ref Str`; its current LLVM path is `Str`-specific. These contracts are observably different. This work does not silently choose one or mark the PRD requirement complete; settle the public slice contract in a separate design step before expanding it.

## Dispatch Principles

- Methods resolve from the receiver type in semantic analysis.
- Receiver borrowing and view-return provenance remain explicit in ownership analysis.
- Add methods in vertical slices: semantic validity, ownership/borrowing, IR/LLVM lowering, native execution, and positive/negative tests.
- Keep unverified entries marked as planned or incomplete; do not equate semantic registration with end-to-end support.
