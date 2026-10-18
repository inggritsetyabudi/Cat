# C@ Implementation Specification (Baseline Subset)

**Baseline:** `main` at `e7789c6efedff677b81c2f883fe8498e32c30554`

**Document date:** 2026-10-18

**Status:** implementation reference for the supported subset; not an amended PRD and not a frozen v1 language specification

This document is a practical reference for the C@ surface that is implemented and tested in this repository. It keeps design intent separate from implementation status: approved decisions remain in [`cat-northstar/PRD.md`](cat-northstar/PRD.md), the v0.1–v1.0 change plan, and the post-v1 roadmap. A feature is described as supported only when compiler paths and tests cover the stated form. The subset is deliberately bounded; pending and incomplete areas remain explicit.

## Scope and evidence

The implementation is in [`claw/src/`](claw/src/). Run the tracked scripts rather than relying on generated output directories:

- Frontend parsing, typing, ownership, and diagnostics: [`claw/tests/frontend/run_frontend_tests.sh`](claw/tests/frontend/run_frontend_tests.sh), with fixtures under [`claw/tests/frontend/fixtures/`](claw/tests/frontend/fixtures/).
- LLVM/backend checks: [`claw/tests/backend/run_backend_tests.sh`](claw/tests/backend/run_backend_tests.sh), with source fixtures under [`claw/tests/backend/fixtures/`](claw/tests/backend/fixtures/).
- Native execution and exact output: [`claw/tests/native/run_native_tests.sh`](claw/tests/native/run_native_tests.sh), with fixtures under [`claw/tests/native/fixtures/`](claw/tests/native/fixtures/).
- Documentation examples and command behavior: [`claw/tests/docs/run_doc_tests.py`](claw/tests/docs/run_doc_tests.py) and [`claw/tests/docs/run_doc_command_tests.py`](claw/tests/docs/run_doc_command_tests.py).

Generated `.ll` and executable files are test outputs, not language evidence that should be linked from this reference. A frontend fixture proves only the checked frontend path; it does not by itself promise LLVM or native support.

## Language names, scalars, and literals

C@ uses lowercase names for variables, functions, fields, and modules. User-defined type names begin with an uppercase letter. Generic/container type constructors are capitalized (`Array`, `Range`, `Span`, `Vec`, `Map`, `Set`, `Result`, `Maybe`); `Ok`, `Fail`, `Some`, and `None` are the only capitalized built-in variants. User-defined `choice` variants are lowercase. These naming categories are case-sensitive except for the closed scalar-name exception below.

The scalar names `Bool`, `Char`, `Unit`, `Int8`, `Int16`, `Int32`, `Int64`, `Int128`, `UInt8`, `UInt16`, `UInt32`, `UInt64`, `UInt128`, `USize`, `Float32`, `Float64`, and `Float128` accept ASCII case variants. `Int` is the alias for `Int64`, `UInt` for `UInt64`, and `Float` for `Float64`; aliases do not create new widths. The case-insensitive rule does not apply to user-defined types, generic parameters, containers, variants, fields, functions, or other identifiers.

Numeric literals support decimal, hexadecimal (`0x`), binary (`0b`), octal (`0o`), `_` separators, and type suffixes such as `255_UInt8` and `2_USize`. Scalar suffix names follow the same closed ASCII case-insensitive rule. Recognizing a scalar type name does not establish every literal, arithmetic operation, or native ABI combination for that type.

`Char` is a Unicode scalar value, not a byte or grapheme cluster. Ordinary strings are UTF-8 `Str` values. Supported escapes are `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\0`, and Unicode scalar escapes of the form `\u{hex}`. Unknown/malformed escapes, invalid UTF-8, surrogate scalars, and values above U+10FFFF are rejected. Embedded NUL is length-aware. Raw line breaks in ordinary strings and f-string interpolation are not supported by this slice. See the [string escape contract](changelog/2026-10-16-v0.4-string-escapes-partial.md).

## Bindings and initialization

`val` declares an immutable binding and `var` declares a mutable binding. An explicitly typed `var` may be declared without an initializer, but it has no default zero value and must be assigned before every read. A moved value, an uninitialized path, or a maybe-uninitialized path is rejected. Use `ref` for a read-only borrow and `ref mut` for an exclusive mutable borrow where the tested path supports it.

Borrow checking is path-based and scoped. Ordinary `shape` declarations cannot contain borrowed fields; local view-shapes and the tested scoped forms are narrower exceptions. General interprocedural lifetime inference is not part of this reference, and `ref mut[s]` remains staged. `Vec` element borrows require a `Span` rather than direct element borrowing.

For a focused `Span[Int32]` borrow-return case, see [`revise_span_borrow.cat`](claw/tests/frontend/fixtures/revise_span_borrow.cat); native mutable indexing has separate evidence in the [Span compound-assignment slice](changelog/2026-10-11-v0.2-index-compound-span.md). Do not mutate a `Vec` while its span is live. `scope s`, `ref[s]`, and local `view shape Name[s]` carriers have tested direct and nested generic paths. Nested generic shape literals require an expected concrete type. `Anchor.new(value)` creates a stable owned cell and `get()` yields `ref T`, within the [nested payload restrictions](changelog/2026-10-12-anchor-nested-payload-safety.md).

## Functions and signatures

A function is declared as `fn name(parameters) -> ReturnType { ... }`. Parameters and calls are positional; named arguments are not supported. Parameter types and the return type are explicit in the supported examples. `main` is the workspace entry function and takes no parameters; the tested forms return `Unit` or `Int32`. A block may use `return expression`; when a value is expected, its final expression is the block value.

```cat
fn add(left: Int32, right: Int32) -> Int32 {
    left + right
}

fn main() -> Int32 {
    val total = add(20, 22)
    println(total)
    return 0
}
```

Function types, closures, broad generic function surfaces, and arbitrary ABI combinations are outside this bounded reference unless a named fixture covers them. Direct calls, the signatures used by ownership/scoped tests, and the native paths above are supported.

## Operators and expression boundaries

The implemented expression subset includes arithmetic, comparisons, `not`/`and`/`or`, indexing, ranges, direct calls, assignment/compound assignment in tested forms, and `if`/`pick` values. The [operator A–B slice](changelog/2026-10-11-v0.2-operator-groups-a-b.md) establishes the following relative precedence, from stronger to weaker:

| Group | Forms | Rule |
| --- | --- | --- |
| Unary | `-`, `not` | Right-associative |
| Multiplicative | `*`, `/`, `%` | Left-associative; `%` requires integers |
| Additive | `+`, `-` | Left-associative |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` | Chained comparisons are rejected |
| Logical AND | `and` | Short-circuit |
| Logical OR | `or` | Short-circuit |

Assignment is a statement, not a chainable value expression. `+=`, `-=`, `*=`, `/=`, and `%=` have supported identifier/field paths, with a separate bounded mutable `Span[Int32]` indexed path. Bitwise operators, `as` casts, `is`, postfix `!`, and a complete overload system must not be inferred from the approved future operator table. `in` and ranges have their own implemented slices. Unsupported operand/type combinations remain errors rather than implicit conversions.

`..` creates an exclusive range and `..=` an inclusive range. `in` and `scan` require compatible element and endpoint types. `if` used as a value requires `else`, and all reachable branch values must have a compatible type. `pick` used as a value must be exhaustive. The implementation supports the tested short-circuit and ownership boundaries; arbitrary expression combinations, dynamic dispatch, and a complete operator-overload library are not promised.

`loop { ... }` repeats unconditionally; `loop condition { ... }` repeats while its condition is true. `stop` exits and `skip` advances to the next iteration. `scan item over iterable` visits a supported iterable; the optional second binding in `scan item, i over iterable` is its zero-based `USize` index.

## Shapes, choices, and casing

A nominal shape is declared with `shape Name { ... }`. Its literal form (`Name { field: value }`) is restricted to the module that defines the shape; callers use the tested constructor/API form such as `Name.new(...)` when available. Borrowed fields are not permitted in ordinary shapes. A `choice` uses lowercase user-defined variants, while `Result` and `Maybe` retain the fixed capitalized variants `Ok`, `Fail`, `Some`, and `None`.

Pattern names are interpreted from the expected type: in a `choice`, a name must be a declared variant, not an accidental binding; `_` is the catch-all. In other typed positions, a lowercase name can bind a value. Tuple and nested patterns are supported only in the tested forms, and non-exhaustive or unreachable `pick` branches are errors.

Tuple literals, typed tuples, positional projections such as `.0`, nested destructuring, and recursive tuple `pick` patterns have implemented slices. Owned tuple cleanup is limited to the validated `Anchor` field cases; arbitrary owned nested aggregates are not implied. See [tuple cleanup](changelog/2026-10-11-v0.3-owned-tuple-cleanup-partial.md) and [nested destructuring](changelog/2026-10-14-v0.3-nested-tuple-destructuring.md).

## Arrays, ranges, and UTF-8 string views

`Array[T, N]` is a fixed-size array whose length is part of its type. The current supported element types are **`Int32` and `UInt8`**, not arbitrary `T`. `[a, b, c]` infers the length when the elements agree; an annotation can provide the expected type. Indexing is bounds-checked, and the tested `scan item` and `scan item, i` forms iterate arrays. The index variable `i` is `USize` and starts at zero. Plain indexed stores are limited to local mutable fixed arrays; compound, global, and nested indexed stores are not generalized support.

`Range[T]` has matching endpoint type `T`; tested paths cover the signed/unsigned integer widths and `Char`. Character scans skip surrogate code points. Floating-point ranges and mixed endpoint types are rejected. String ranges are limited to `Range[Int32]` and use **byte offsets**: the normalized half-open start and end must be UTF-8 boundaries. Invalid direct string-range indexing triggers a runtime defect; `text.get(range) -> Maybe[ref Str]` returns `None` for invalid or out-of-range requests and `Some` with a borrowed view for valid ranges. Inclusive ranges normalize the exclusive end to `b + 1`. This is not a general collection slicing API.

```cat
fn sum(values: Array[Int32, 3]) -> Int32 {
    var total: Int32 = 0
    scan item over values {
        total += item
    }
    return total
}

fn main() -> Int32 {
    val values: Array[Int32, 3] = [7, 128, 255]
    println(sum(values))

    scan item, i over 0..=2 {
        println(i)
        println(item)
    }

    val text: Str = "AéB"
    println(text[0..1])
    println(text[1..=2])
    return 0
}
```

The fixed-array evidence includes [`revise_fixed_array.cat`](claw/tests/native/fixtures/revise_fixed_array.cat) and [`revise_fixed_array_uint8.cat`](claw/tests/native/fixtures/revise_fixed_array_uint8.cat). The UTF-8 boundary evidence is in [`revise_str_range_views.cat`](claw/tests/native/fixtures/revise_str_range_views.cat) and its negative companions; that fixture does not establish `Span` behavior.

## `Result`, `Maybe`, `try`, and `pick`

`Result[T, E]` has `Ok(value)` and `Fail(error)`; `Maybe[T]` has `Some(value)` and `None`. Both are must-use: ignoring them as bare statement values is rejected. Handle them, bind them, or explicitly discard them. `try` is permitted only as the initializer of a `val` and its operand must be `Result`.

The shorthand `val value = try expression` requires the enclosing function to return `Result` with an **identical error type**; it performs no implicit error conversion. In `val value = try expression else err { ... }`, the error is handled locally and the handler must terminate the current control path. The local handler is not subject to the shorthand's enclosing-error-type rule. Each `try` unwraps one `Result`; `var x = try ...`, `return try ...`, and nested expression chaining are rejected.

```cat
fn read_value(ok: Bool) -> Result[Int32, Str] {
    if ok {
        return Ok(37)
    }
    return Fail("read-failed")
}

fn use_value(ok: Bool) -> Result[Int32, Str] {
    val value = try read_value(ok)
    return Ok(value + 1)
}

fn main() {
    val result = use_value(true)
    pick result {
        Ok(value) { println(value) }
        Fail(error) { println(error) }
    }
}
```

The complete ownership and local-error form is exercised by [`revise_try_contract.cat`](claw/tests/native/fixtures/revise_try_contract.cat) and [`revise_try_owned_result.cat`](claw/tests/native/fixtures/revise_try_owned_result.cat). `if` and `pick` value lowering and ownership joins are covered by [`revise_if_pick_values.cat`](claw/tests/native/fixtures/revise_if_pick_values.cat).

## Modules and workspaces

A standalone `.cat` file can be checked or built directly. A workspace contains `claw.toml` and exactly one root `main.cat`; directories below the root form namespaces. Each source file is one module. There is **no required `src/modules.cat` index**, and `main.cat` is entry-only: it cannot be imported and its `main` function is not an ordinary callable module function.

Imports are explicit and path-based. `import src.math as math_ops` imports the module at `src/math.cat` under an alias; `import super.{ sibling }` addresses a sibling module from a file in the same directory. An import alias must be unique in its file. A declaration is importable only when marked `share`; private declarations do not leak through an import. Imported names are used through the module alias, and unresolved paths, duplicate names, cycles, and visibility violations are errors. The workspace must have one valid root entry and must not define a second entry through an imported file.

The tracked module fixture is [`revise_module_aliases`](claw/tests/frontend/fixtures/revise_module_aliases/), and the workspace checks are run by the frontend/native scripts. The fixture's two module files demonstrate explicit aliases and `share`; generated build artifacts are intentionally not part of this reference.

## Backend and implementation boundary

The compiler has tested LLVM emission and native linking for single-file revised programs, root-entry workspaces, direct calls, arithmetic, tuples, fixed arrays, integer/character ranges, `Result`/`Maybe` control flow, scoped references, and `Anchor` paths named by the suites. The native profile is the tested `clang -O2` path; this document does not promise selectable debug/release profiles or alternate backends.

The PR #21 borrowed `Str` / owned `String` foundation is on this baseline. String literals are static borrowed `Str`; `String.from` explicitly copies into owned storage, `as_str` borrows from the owner, and clone/move/cleanup follow the tested [ownership slice](changelog/2026-10-17-str-string-ownership.md). Generic collection storage of `Str`/`String` remains rejected pending borrow/drop propagation. No implicit borrowed-to-owned conversion, interpolation, or arbitrary owned projection is implied. Per-slice labels and evidence are maintained in the [G2 inventory](cat-northstar/FEATURE-STATUS.md).

The following remain bounded, incomplete, or staged: Arena; general lifetime inference; borrowed fields in normal shapes; `ref mut[s]`; full `Vec`/`Map`/`Set`/`Queue` literal and method surfaces; complete imported-workspace native coverage; broad raw/FFI lowering; threading, channels, macros, and self-hosting. Arena has no supported API guarantee here. Open PRs #22–#26 are not part of this implementation baseline.

Future documentation changes must first be backed by implementation and tests, then recorded in a dated changelog entry. This reference intentionally describes the current supported subset rather than declaring v1 complete.
