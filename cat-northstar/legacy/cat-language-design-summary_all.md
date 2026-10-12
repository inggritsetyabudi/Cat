# C@ Language Design — Summary of Changes

## Slogan
**Fast, Safe, Simple**

---

## 1. Syntax Fungsi & Blok

| Aspek | PRD Asal | Revisi |
|---|---|---|
| Parameter fungsi | `fn add[a: Int32, b: Int32]` | `fn add(a: Int32, b: Int32)` |
| Blok | `[` `]` | `{` `}` |
| Return | `give value` | `return value` |

```
// Before
fn add[left: Int32, right: Int32] -> Int32 [
    give left + right
]

// After
fn add(left: Int32, right: Int32) -> Int32 {
    return left + right
}
```

---

## 2. Borrow / View System

| Aspek | PRD Asal | Revisi |
|---|---|---|
| Borrow read-only | `look T` | `ref T` |
| Borrow mutable | `edit T` | `ref mut T` |
| Owned | `T` | `T` (tetap sama) |

```
fn len(buf: ref Buffer) -> Int32 { ... }       // read-only
fn push(buf: ref mut Buffer, val: UInt8) { ... } // mutable
fn consume(buf: Buffer) { ... }                 // takes ownership
```

**Alasan:** `mut` konsisten di seluruh bahasa — muncul di `var` untuk mutable binding, dan di `ref mut` untuk mutable borrow.

---

## 3. Tipe Primitif

### Numerik

| Kategori | Tipe | Alias Default |
|---|---|---|
| Integer | `Int8` `Int16` `Int32` `Int64` `Int128` | `Int` = `Int64` |
| Unsigned | `UInt8` `UInt16` `UInt32` `UInt64` `UInt128` | `UInt` = `UInt64` |
| Float | `Float32` `Float64` `Float128` | `Float` = `Float64` |

### Lainnya

| Tipe | Keterangan |
|---|---|
| `Bool` | true / false |
| `Char` | unicode codepoint (ganti `Rune`) |
| `Str` | string (ganti `Text`) |
| `USize` | ukuran platform |
| `Unit` | no value |

### Dihapus dari PRD Asal
`Bits8`, `Bits16`, `Bits32`, `Bits64`, `Bits128`, `Byte`, `Bytes`, `ISize`, `Rune`

**Tidak ada `Long`, `Short`** — ukuran selalu eksplisit di nama tipe.

---

## 4. Koleksi & Generic System

### Tipe Koleksi

| Tipe | Keterangan | Literal |
|---|---|---|
| `Array[T]` | Fixed size, stack-allocated | `[1, 2, 3]` |
| `Vec[T]` | Dynamic, heap-allocated | `Vec[1, 2, 3]` |
| `Span[T]` | View/slice, tidak owning | — |
| `Map[K, V]` | HashMap | `Map{"a": 1, "b": 2}` |
| `Set[T]` | Unique values | `Set[1, 2, 3]` |
| `Queue[T]` | FIFO queue | `Queue.new()` |
| `(T, U)` | Tuple | `(10, "hello")` |

### Konversi ke Span — Eksplisit
```
val view: Span[Int32] = scores.span()
val view: Span[Int32] = names.span()
```

### Akses Tuple
```
val point: (Int32, Int32) = (10, 20)
val x = point.0
val y = point.1
```

### Built-in Generic Types
```
// Maybe[T] — absence / optional
choice Maybe[T] {
    None
    Some(value: T)
}

// Result[T, E] — recoverable failure
choice Result[T, E] {
    Ok(value: T)
    Fail(cause: E)
}
```

### Konvensi Kapital
Semua konstruktor variant, koleksi, dan tipe diawali **huruf kapital**:
`Some(v)`, `None`, `Ok(v)`, `Fail(e)`, `Vec`, `Map`, `Set`, `Queue`

---

## 5. Error Handling

### Sebelum — verbose dan kompleks
```
lift fs.open(path) as file fail issue [
    give fail(issue)
]
```

### Sesudah — dua level

**Shorthand** — propagate otomatis ke caller (90% kasus):
```
val file = try fs.open(path)
```

**Full form** — kalau butuh custom handling (10% kasus):
```
val file = try fs.open(path) else err {
    logger.warn(err)
    return Fail(err)
}
```

---

## 6. Module & Import System

### Struktur Workspace
```
my-app/
├── main.cat              ← entry point wajib, berisi fn main()
└── src/
    ├── modules.cat
    ├── math.cat
    ├── api/
    │   ├── modules.cat
    │   ├── router.cat
    │   └── handler.cat
    └── crypto/
        ├── modules.cat
        └── hasher.cat
```

### `modules.cat` — Barrel Export
Hanya modul yang di-list yang bisa diimport dari luar folder:
```
pub modules { router, handler, middleware }

// atau satu modul
pub modules math
```

### Import Syntax
```
// Sibling di folder yang sama
import super.{ utils, Config }

// Path absolut dari root, item spesifik
import src.api.{ Router, Handler }

// Semua public (filter by `share`) dari modul
import src.math

// Dengan alias
import src.crypto.{ Hasher as H }
```

### Aturan Import
| Konteks | Syntax |
|---|---|
| Folder yang sama | `import super.{ ... }` |
| Cross-folder | `import src.x.y.{ ... }` — selalu dari `src.` |

**`realm` dihapus** — lokasi file sudah cukup menjelaskan namespace.

---

## 7. Yang Tetap Dipertahankan dari PRD Asal [sudah]

> [sudah] Semua syntax legacy `of` sudah ditolak secara eksplisit oleh compiler. Generic dan choice murni menggunakan sintaks `[]` (`Vec[T]`, `Result[T, E]`).

| Fitur | Syntax |
|---|---|
| Immutable binding | `val name = value` |
| Mutable binding | `var name = value` |
| Conditional | `if condition { } else { }` |
| Loop | `loop condition { }` |
| Infinite loop | `loop { }` |
| Iteration | `scan item over items { }` |
| Loop control | `stop`, `skip` |
| Shape | `shape Name { ... }` |
| Choice / tagged union | `choice Name[T] { ... }` |
| Pattern match | `pick value { pattern { } }` |
| Public field | `share field: Type` |
| Generic syntax | `[]` (contoh: `Vec[T]`) |
| Raw region | `raw { ... }` |
| Unsafe boundary | `foreign c { ... }` |

---

## 8. Contoh Kode Lengkap — Final

```
// main.cat
import src.demo.{ word_count }

fn main() -> Int32 {
    val words: Vec[Str] = Vec["hello", "world", "hello"]
    val counts = word_count(words)
    println("done")
    return 0
}

// src/demo.cat
shape Config {
    share host: Str
    share port: Int32
}

choice Auth[T] {
    Guest
    User(value: T)
}

fn show_auth(state: Auth[Config]) {
    pick state {
        Guest {
            println("guest")
        }
        User(cfg) {
            println(cfg.host)
        }
    }
}

fn word_count(words: ref Vec[Str]) -> Map[Str, Int32] {
    var counts: Map[Str, Int32] = Map.new()
    scan word over words.span() {
        val current = map.get(counts, word)
        pick current {
            None {
                map.set(counts, word, 1)
            }
            Some(n) {
                map.set(counts, word, n + 1)
            }
        }
    }
    return counts
}

fn load_text(path: ref Str) -> Result[Str, IoFault] {
    val file = try fs.open(path) else err {
        return Fail(err)
    }
    return Ok(fs.read_all(file))
}
```

---

## Ringkasan Perubahan vs PRD Asal

| # | Aspek | Sebelum | Sesudah |
|---|---|---|---|
| 0 | Binding | `val`/`var` | `val`/`var` — familiar seperti Kotlin/Swift |
| 1 | Parameter | `fn f[a: T]` | `fn f(a: T)` |
| 2 | Blok | `[ ]` | `{ }` |
| 3 | Return | `give value` | `return value` |
| 4 | Borrow read | `look T` | `ref T` |
| 5 | Borrow mut | `edit T` | `ref mut T` |
| 6 | String type | `Text` | `Str` |
| 7 | Unicode char | `Rune` | `Char` |
| 8 | Tipe dihapus | `Bits*`, `Byte`, `Bytes`, `ISize` | — |
| 9 | 128-bit | Tidak ada | `Int128`, `UInt128`, `Float128` |
| 10 | Error propagation | `lift ... as ... fail ... [ ]` | `try ... else err { }` |
| 11 | Result type | `Result[T, E]` | `Result[T, E]` |
| 12 | Namespace | `realm net.http` | Tidak perlu, path = namespace |
| 13 | Import | `link core.io` | `import src.io` |
| 14 | Print | `io.print(...)` | `print(...)` / `println(...)` |
| 15 | Koleksi baru | Hanya `Vec` | `Array`, `Vec`, `Map`, `Set`, `Queue`, `Tuple` |
| 16 | Kapital | `some(v)`, `none` | `Some(v)`, `None`, `Ok(v)`, `Fail(e)` |
| 17 | Generic syntax | `Vec of T`, `Result of T, E` | `Vec[T]`, `Result[T, E]` |
| 18 | Conditional | `when/otherwise` | `if/else` |
| 19 | Lifetime | Lexical scope saja | Full inference — compiler tentukan, programmer tidak tulis apapun |
| 20 | Edge cases | Tidak ada solusi khusus | `Anchor[T]`, `scope` block, Arena |
| 21 | FFI detail | `foreign c` + `raw` dasar | Aturan lengkap — raw tidak boleh bocor ke public API |
| 22 | String literal | `Str.from("hello")` | `"hello"` langsung jadi Str |
| 23 | Static | Belum ada | Immutable, level file saja, lifetime = program |
| 36 | Binding | `hold` / `slot` | `val` (immutable) / `var` (mutable) — familiar seperti Kotlin/Swift |
| 24 | main.cat | Belum ada aturan spesial | File spesial — tidak bisa diimport, `fn main` wajib ada tepat satu, dan return boleh implicit `Unit`, explicit `-> Unit`, atau `-> Int32` |
| 25 | Static init | Belum ada | Compile-time only — tidak ada runtime initialization |
| 26 | Thread | Belum ada aturan | Boleh spawn dari mana saja, main wajib join sebelum return |
| 27 | Method pada shape | `impl` (Rust-style) | `implements Shape { }` — di file yang sama |
| 28 | Trait/Interface | Tidak ada | `contract` — kontrak eksplisit yang harus dipenuhi |
| 29 | Self parameter | `self: &Self` | `self: ref` atau `self: ref mut` — tipe diinferensi |
| 30 | Operator overloading | Tidak ada | Lewat `contract` Add, Sub, Equal, Order, dll |
| 31 | String interpolation | Tidak ada | `f"Hello {name}"` |
| 32 | Anonymous function | Tidak ada | `fn(x: T) -> R { }` — tanpa capture di v1 |
| 33 | Type casting | Tidak ada | `x as Int64` untuk numerik, constructor untuk lainnya |
| 34 | Defect mode | Tidak ada | `abort` atau `unwind` dikonfigurasi di claw.toml |
| 35 | Komentar | Tidak ada | `//` `/* */` `///` doc comment |

---

## 9. Lifetime Inference — Full Compiler Inference

Programmer **tidak pernah** menulis lifetime annotation. Compiler inferensi sepenuhnya.

### Tiga Aturan Compiler

**Aturan 1 — Single ref input:** Output ref pasti berasal dari satu-satunya input.
```
fn get_name(user: ref User) -> ref Str {
    return ref user.name    // compiler: lifetime output = lifetime user
}
```

**Aturan 2 — Multiple ref input:** Compiler ambil lifetime terpendek — paling conservative.
```
fn longest(a: ref Str, b: ref Str) -> ref Str {
    if a.len() > b.len() { return a }
    return b
    // compiler: lifetime output = min(lifetime a, lifetime b)
}
```

**Aturan 3 — Tidak bisa diinferensi:** Compiler error dengan pesan jelas.
```
shape Cache {
    stored: ref Str    // ERROR — compiler tidak bisa tentukan
                       // lifetime ref yang disimpan di struct
}

// Solusi: simpan owned value
shape Cache {
    stored: Str        // OK — owned, lifetime jelas
}
```

### Contoh Error yang Terdeteksi
```
fn bad(x: ref Str) -> ref Str {
    val local = Str.from("hello")
    return ref local    // ERROR — local mati saat fungsi selesai
                        // ref akan dangling!
}
```

### Trade-off
- ✅ Programmer tidak perlu pelajari lifetime syntax sama sekali
- ✅ 95% kasus production bisa diinferensi otomatis
- ✅ Error tetap terdeteksi compile time — tidak ada runtime surprise
- ❌ 5% kasus edge (ref dalam struct, complex aliasing) harus direstruktur jadi owned
- ❌ Compiler lebih kompleks — tapi itu urusan pembuat compiler, bukan programmer

---

## 10. `foreign c` dan `raw {}` — Unsafe Boundary

### `foreign c` — Deklarasi FFI

Dipakai untuk mendeklarasikan fungsi dari library C eksternal.
Hanya berisi **deklarasi**, bukan implementasi.
Semua tipe di dalamnya harus tipe raw karena C tidak kenal ownership C@.

```
foreign c {
    fn malloc(size: USize) -> RawPtr[UInt8]
    fn free(ptr: RawPtr[UInt8])
    fn strlen(s: RawPtr[UInt8]) -> USize
    fn memcpy(dst: RawMut[UInt8], src: RawPtr[UInt8], n: USize)
}
```

**Tipe raw yang tersedia di dalam foreign/raw:**

| Tipe | Keterangan |
|---|---|
| `RawPtr[T]` | Pointer read-only ke T, tanpa ownership |
| `RawMut[T]` | Pointer mutable ke T, tanpa ownership |
| `Addr` | Alamat mentah tanpa tipe |

### `raw {}` — Blok Tidak Aman

Di dalam `raw {}`, compiler mematikan semua pemeriksaan:
- Ownership checker dimatikan
- Lifetime inference dimatikan
- Null check dimatikan
- Bounds check dimatikan

Programmer bertanggung jawab penuh atas kebenaran kode di dalamnya.

```
fn env_get(name: ref Str) -> Maybe[Str] {
    raw {
        val ptr = libc.getenv(name.as_raw_ptr())
        if ptr == Addr.null() {
            return None
        }
        return Some(Str.from_raw_ptr(ptr))
    }
}
```

### Aturan Wajib — Raw Harus Terbungkus

`RawPtr`, `RawMut`, dan `Addr` **tidak boleh bocor** ke public API.
Semua `raw {}` harus dibungkus fungsi safe di luar.

```
// SALAH — RawPtr bocor ke public API
share fn get_ptr() -> RawPtr[UInt8] {    // ERROR di compile time
    raw { return malloc(100) }
}

// BENAR — dibungkus safe interface
share fn alloc_buffer(size: USize) -> Maybe[Vec[UInt8]] {
    raw {
        val ptr = malloc(size)
        if ptr == Addr.null() { return None }
        return Some(Vec.from_raw_parts(ptr, size))
    }
}
```

### Error yang Terdeteksi

```
// ERROR 1 — RawPtr keluar dari fungsi tanpa raw wrapper
share fn leak_ptr(x: RawPtr[UInt8]) -> RawPtr[UInt8] {
    return x    // ERROR: RawPtr tidak boleh di public signature
}

// ERROR 2 — foreign fn dipanggil di luar raw
fn bad() {
    malloc(100)    // ERROR: foreign fn hanya boleh dipanggil di dalam raw {}
}

// ERROR 3 — dangling ref di dalam raw (tidak dicek compiler, tapi runtime crash)
fn dangerous() -> ref Str {
    raw {
        val local = Str.from("oops")
        return ref local    // BUG — local mati saat fungsi selesai
                            // compiler tidak cek di dalam raw!
                            // ini tanggung jawab programmer
    }
}
```

### Interaksi dengan Lifetime Inference

Di luar `raw {}` — compiler inferensi lifetime secara penuh.
Di dalam `raw {}` — lifetime inference dimatikan total.
Ini yang membuat `raw {}` berbahaya dan harus seminimal mungkin.

---

## 11. Solusi Edge Cases — `Anchor`, `scope`, Arena [sudah]

> [sudah] `Arena` memory pool khusus self-referential telah diintegrasikan di compiler, semantic type-checker (mendukung track bounds parameter asal), dan block chunked allocator (bump pointer tanpa GC) di runtime. `Anchor` dan `scope` juga telah berfungsi.

> [sudah] Compiler sekarang sudah mendukung `scope s {}`, `ref[s]`, propagasi scoped ref melalui call/return, dan error escape/source lifetime yang informatif.
>
> [sudah] Compiler sekarang sudah mendukung `Anchor.new(value)`, `anchor.get() -> ref T`, validasi payload `Anchor[T]`, serta drop heap otomatis saat `Anchor` keluar dari scope.
>
> [sudah] Compiler sekarang sudah mendukung `view shape Name[s] { ... }`, konstruksi lokal `Name { ... }` di dalam `scope s`, dan penolakan escape untuk nilai scoped aggregate.
>
> [sudah] Propagasi scoped type langsung tervalidasi: `Name[s]` dan `ref[s]` dapat dipakai pada binding, field view-shape, parameter/return, call lintas fungsi, serta signature shared/imported. Scope pada signature adalah binder formal yang dipetakan ke scope aktif pemanggil; binder berulang harus konsisten. Return yang tidak terikat ke parameter, mismatch, escape, dan sumber berumur terlalu pendek ditolak; frontend, LLVM, dan native workspace diuji.
>
> [sudah] Nested scoped generic `Box[Name[s]]` kini didukung end-to-end untuk literal generic yang memiliki expected concrete type: binder nested dipropagasikan rekursif, concrete layout dan LLVM ABI parameter/return/call dilower, serta akses field dan signature shared/imported lintas modul tervalidasi. Binder/field mismatch, return escape, dan literal tanpa expected type ditolak; frontend, verifikasi LLVM IR, dan native execution single-file/workspace lulus. Lifetime inference umum tetap tidak diperluas.

### Kasus yang Tidak Bisa Diinferensi Otomatis

Lifetime inference bekerja untuk 95% kasus. Lima persen sisanya:
- Ref yang disimpan di dalam struct
- Multiple ref dari sumber berbeda yang hidup bersamaan
- Self-referential structures (linked list, tree, graph)

### Solusi 1 — `Anchor[T]` (Ref dalam Struct)

`Anchor[T]` menyimpan nilai di heap dengan **alamat stabil** — nilai tidak bisa dipindah setelah dialokasi. Compiler tahu ref ke dalamnya aman selama pemilik `Anchor` hidup.

> [sudah] **Final Form Anchor:** Method `anchor.get()` secara langsung mengembalikan tipe `ref T`. Syntax pointer manual desain lama telah ditinggalkan. Ini mempermudah *method dispatch* dan sangat natural bagi borrow checker serta IR backend.

```
// MASALAH — compiler tidak bisa inferensi lifetime ref dalam struct
shape Cache {
    stored: ref Str    // ERROR: compiler tidak tahu lifetime ref ini
}

// SOLUSI — pakai Anchor
shape Cache {
    stored: Anchor[Str]    // OK — heap-backed, alamat stabil
}

fn make_cache(val: Str) -> Cache {
    return Cache(stored: Anchor.new(val))
}

fn get(cache: ref Cache) -> ref Str {
    return cache.stored.get()
    // compiler inferensi: cache.stored.get() langsung return 'ref Str'
    // lifetime output = lifetime cache — OK
}
```

> [sudah] Compiler saat ini sudah menolak borrowed field pada shape biasa dan menerima `Anchor[T]` sebagai carrier owned yang stabil.

**Cara kerja internal:**
- `Anchor.new(val)` alokasi `val` di heap
- Alamat heap tidak berubah sampai `Anchor` dihancurkan
- Destructor `Anchor` otomatis bebaskan heap saat scope selesai
- Tidak perlu `free()` manual

**Error yang terdeteksi:**
```
// ERROR — ref ke Anchor keluar dari lifetime pemiliknya
fn bad() -> ref Str {
    val cache = Cache(stored: Anchor.new("hello"))
    return cache.stored.get()
    // ERROR: cache mati di akhir fungsi, ref akan dangling
}

// BENAR — return owned value
fn good() -> Str {
    val cache = Cache(stored: Anchor.new("hello"))
    return cache.stored.get().clone()    // clone = ambil salinan
}
```

---

### Solusi 2 - `scope` Block (Multiple Ref Bersamaan)

> [sudah] Compiler saat ini sudah mendukung `scope`, `ref[s]`, dan `view shape Name[s]` sebagai carrier aggregate lokal untuk beberapa borrow yang hidup bersamaan.
>
> [sudah] View-shape `Name[s]` kini juga tervalidasi langsung di binding/field view-shape, function parameter/return, call, dan shared/imported signature. Binder scope dipetakan ke scope aktual secara konsisten dan tetap tunduk pada pemeriksaan escape/lifetime.
>
> [sudah] Pembungkusan scoped view-shape di generic lain (`Box[Name[s]]`) didukung end-to-end, termasuk layout/ABI indirect yang diuji dan workspace lintas modul. Konstruksi generic memerlukan expected concrete type; inferensi lifetime umum tetap di luar cakupan milestone ini.

Untuk kasus di mana beberapa ref dari sumber berbeda harus hidup dalam rentang waktu yang sama. Programmer deklarasi satu scope eksplisit, semua ref terikat ke scope itu.

```
// MASALAH — parser butuh dua ref yang hidup bersamaan
view shape Parser[s] {
    input: ref[s] Str
    current: ref[s] Str
}

// SOLUSI — scope block + view shape
fn parse(input: ref Str) -> Result[Token, ParseFault] {
    scope s {
        val parser = Parser {
            input:   ref[s] input,
            current: ref[s] input
        }
        // parser hanya valid di dalam scope s
        return run_parser(ref mut parser)
    }
    // scope s selesai — semua ref[s] otomatis invalid di sini
}
```

**Syntax `ref[s]`** — ref yang terikat ke scope bernama `s`.
Compiler verifikasi: tidak ada `ref[s]` yang keluar dari blok `scope s { }`.

**Cara kerja:**
```
scope s {
    val a: ref[s] Str = ref source_a    // terikat ke s
    val b: ref[s] Str = ref source_b    // terikat ke s

    process(a, b)    // OK — keduanya masih hidup

}   // s selesai — a dan b otomatis invalid
    // compiler pastikan tidak ada yang pakai a/b setelah ini
```

**Error yang terdeteksi:**
```
// ERROR 1 — ref[s] keluar dari scope
scope s {
    val x: ref[s] Str = ref source
    return x    // ERROR: ref[s] tidak boleh keluar dari scope s
}

// ERROR 2 — ref[s] disimpan di luar scope
var saved: ref Str = ???
scope s {
    val x: ref[s] Str = ref source
    saved = x    // ERROR: ref[s] tidak boleh assign ke binding luar scope
}

// ERROR 3 — source mati sebelum scope selesai
scope s {
    val x: ref[s] Str = {
        val local = Str.from("hi")
        ref[s] local    // ERROR: local mati sebelum scope s selesai
    }
}
```

**Keuntungan vs Rust:**
```
// Rust — lifetime annotation tersebar di mana-mana
struct Parser<'a> {
    input: &'a str,
    current: &'a str,
}
fn parse<'a>(input: &'a str) -> Token { ... }

// C@ — cukup satu scope block dan satu view shape, tidak ada annotation lifetime di signature
fn parse(input: ref Str) -> Result[Token, ParseFault] {
    scope s {
        val parser = Parser { input: ref[s] input, current: ref[s] input }
        ...
    }
}
```

---

### Solusi 3 — Arena (Self-Referential Structures)

Untuk linked list, tree, graph — struktur yang node-nya saling referensi satu sama lain.
Semua node dialokasi di satu **pool memori** (arena).
Ref antar node valid selama arena hidup.
Seluruh arena dihancurkan sekaligus di akhir — tidak ada destructor per-node.

```
// MASALAH — self-referential struct
shape Node {
    data: Int32
    next: Maybe[ref Node]    // ERROR — ref ke Node lain tidak bisa diinferensi
}

// SOLUSI — Arena
fn build_list(arena: ref mut Arena) -> ref Node {
    val n1 = arena.alloc(Node(data: 1, next: None))
    val n2 = arena.alloc(Node(data: 2, next: Some(ref n1)))
    val n3 = arena.alloc(Node(data: 3, next: Some(ref n2)))
    return ref n3
    // semua ref valid selama arena hidup — compiler tahu ini
}

fn main() -> Int32 {
    val arena = Arena.new()
    val list = build_list(ref mut arena)
    traverse(ref list)
    // arena dihancurkan di sini — semua node bebas sekaligus
    return 0
}
```

**Cara kerja internal:**
- `Arena.new()` alokasi blok memori besar di heap
- `arena.alloc(val)` taruh `val` di dalam blok itu — alamat stabil
- Tidak ada destructor per-node
- Saat `Arena` dihancurkan, seluruh blok dibebaskan sekaligus — sangat cepat

**Cocok untuk:**
- Linked list, doubly linked list
- Binary tree, N-ary tree
- Graph dengan banyak edge
- AST (Abstract Syntax Tree) di compiler
- Temporary data structures yang hidup singkat

**Error yang terdeteksi:**
```
// ERROR 1 — ref dari arena keluar dari lifetime arena
fn bad(arena: ref mut Arena) -> ref Node {
    val n = arena.alloc(Node(data: 1, next: None))
    return ref n    // OK selama caller pastikan arena hidup lebih lama

    // Tapi ini ERROR:
    val local_arena = Arena.new()
    val m = local_arena.alloc(Node(data: 2, next: None))
    return ref m    // ERROR: local_arena mati di akhir fungsi
}

// ERROR 2 — arena dialokasi ulang setelah ref diambil
val arena = Arena.new()
val node = arena.alloc(Node(data: 1, next: None))
arena.reset()      // ERROR: ada ref aktif ke dalam arena
                   // reset akan invalidate semua ref yang ada
```

---

## Ringkasan — Kapan Pakai Apa

| Situasi | Solusi | Kompleksitas |
|---|---|---|
| 95% kasus normal | Compiler inferensi otomatis | Tidak perlu apa-apa |
| Ref dalam struct | `Anchor[T]` | Rendah — ganti tipe saja |
| Multiple ref bersamaan | `scope` block | Sedang — tambah satu blok |
| Linked list / tree / graph | Arena | Sedang — pakai pool allocator |
| FFI / low-level C interop | `foreign c` + `raw {}` | Tinggi — programmer penuh tanggung jawab |


---

## 12. String Literal dan `static` [sudah]

> [sudah] String literal (`Str`) teralokasi di read-only data segment. `static` keyword telah diimplementasikan end-to-end sebagai immutable global compile-time constant seumur program.

### String Literal — Langsung `""`

Tidak perlu constructor. Double quote langsung menghasilkan `Str`.

```
val msg = "hello"                    // Str, inferensi otomatis
val name: Str = "C@"                 // Str, eksplisit
val greeting = "Halo, " + name       // concatenation
```

### `static` — Nilai yang Hidup Sepanjang Program

```
static VERSION: Str = "1.0.0"
static MAX_RETRY: Int32 = 3
static BASE_URL: Str = "https://api.example.com"
```

**Tiga aturan `static`:**

**Aturan 1 — Selalu immutable.**
`static` tidak bisa dikombinasikan dengan `var`. Mutable global state
dilarang karena berbahaya di concurrent code dan melanggar prinsip *Safe*.

```
var static counter: Int32 = 0    // ERROR — static tidak boleh mutable
```

Kalau butuh shared mutable state, gunakan tipe sinkronisasi dari stdlib:
```
val counter = Mutex[Int32].new(0)    // benar — eksplisit dan thread-safe
```

**Aturan 2 — Hanya di level file/module, tidak di dalam fungsi.**
`static` di dalam fungsi menciptakan hidden state yang tidak bisa diaudit
dan melanggar prinsip *Explicit boundaries*.

```
// BENAR — di level file
static VERSION: Str = "1.0.0"

// SALAH — di dalam fungsi
fn count() -> Int32 {
    static n: Int32 = 0    // ERROR — hidden state tidak diizinkan
    n = n + 1
    return n
}
```

**Aturan 3 — Ref ke `static` selalu valid.**
Compiler tahu lifetime `static` = lifetime program, sehingga ref ke
`static` tidak perlu diinferensi dan selalu aman.

```
fn get_version() -> ref Str {
    return ref VERSION    // OK — VERSION hidup selamanya
}

fn get_url() -> ref Str {
    return ref BASE_URL   // OK — tidak ada lifetime issue
}
```

### Error yang Terdeteksi

```
// ERROR 1 — static mutable
var static MAX: Int32 = 100          // ERROR: static harus immutable

// ERROR 2 — static di dalam fungsi
fn init() {
    static READY: Bool = false        // ERROR: static hanya di level file
}

// ERROR 3 — static dengan tipe yang tidak plain atau Str
static handler: Fn[Int32] -> Unit = process  // ERROR: static hanya untuk
                                              // nilai compile-time constant
```


---

## 13. main.cat  Entry Point [sudah]

> [sudah] Compiler saat ini menerima `fn main()` dengan implicit `Unit`, explicit `-> Unit`, atau `-> Int32` untuk exit code OS.

### Aturan Spesial `main.cat`

`main.cat` adalah file spesial yang berbeda dari semua file `.cat` lainnya.
Dia adalah **program**, bukan library.

**Aturan 1 — `fn main` wajib ada, tanpa parameter, dengan salah satu bentuk return yang sah:**
```
fn main() {
}

fn main() -> Unit {
}

fn main() -> Int32 {
    return 0
}
```

**Aturan 2 — `fn main` hanya boleh ada di `main.cat`:**
```
// src/utils.cat
fn main() -> Int32 { ... }    // ERROR — fn main hanya boleh di main.cat
```

**Aturan 3 — `main.cat` tidak bisa diimport siapapun:**
```
// src/api/router.cat
import main.{ something }    // ERROR — main.cat tidak bisa diimport
```

Ini menegaskan bahwa `main.cat` bukan library — dia adalah titik masuk program,
bukan komponen yang bisa dipakai ulang.

### Static Initialization — Compile-time Only

`static` hanya boleh diinisialisasi dengan nilai yang diketahui saat compile.
Tidak ada runtime initialization untuk `static` — nilai selalu siap sejak
detik pertama program jalan. Tidak ada urutan inisialisasi yang perlu dipikirkan.

```
// OK — nilai diketahui saat compile
static VERSION: Str = "1.0.0"
static MAX_CONN: Int32 = 1024
static BASE_PATH: Str = "/var/app"

// ERROR — butuh runtime untuk evaluasi
static CONFIG: Config = Config.load("config.toml")    // ERROR
static RANDOM: Int32 = rand.next()                    // ERROR
```

Kalau butuh nilai runtime saat startup, itu urusan variabel biasa di `main` —
bukan `static`:
```
fn main() -> Int32 {
    val cfg = Config.load("config.toml")    // runtime — pakai val biasa
    val server = Server.new(cfg)
    return 0
}
```

### Thread Spawning

Thread boleh di-spawn dari fungsi manapun — tidak harus dari `main`.
Tapi `main` **wajib join semua top-level thread** sebelum return.
Kalau `main` return sementara ada thread yang masih jalan,
runtime akan tunggu sampai semua thread selesai.

```
fn main() -> Int32 {
    val cfg = Config.load("config.toml")
    val server = Server.new(cfg)

    val t = thread.spawn(fn server.run)

    thread.join(t)    // wajib join sebelum return
    return 0
}

// Thread boleh spawn thread lagi dari dalam fungsi lain
fn run(server: ref mut Server) {
    loop {
        val conn = server.accept()
        thread.spawn(fn handle_conn(conn))    // OK — boleh dari sini
    }
}
```

**Aturan thread:**
- Thread boleh di-spawn dari fungsi manapun
- `main` harus join semua top-level thread sebelum return
- Runtime tunggu semua thread selesai sebelum program benar-benar exit
- Data yang di-pass ke thread harus `sendable` — compiler enforce ini

### Error yang Terdeteksi

```
// ERROR 1 — fn main tidak ada
// main.cat kosong atau tidak punya fn main = compile error

// ERROR 2 — fn main signature salah
fn main(args: Vec[Str]) -> Int32 { ... }    // ERROR — tidak boleh ada parameter

// ERROR 3 — fn main di file lain
// src/utils.cat
fn main() -> Int32 { ... }                  // ERROR

// ERROR 4 — import main.cat
import main.{ VERSION }                     // ERROR

// ERROR 5 — static diinisialisasi dengan nilai runtime
static CFG: Config = Config.load("x")      // ERROR — runtime value

// ERROR 6 — pass non-sendable ke thread
shape Handler {
    data: ref Str    // ref tidak sendable
}
thread.spawn(fn handler.run)               // ERROR — Handler tidak sendable
```

### Contoh `main.cat` Lengkap

```
import src.server.{ Server }
import src.config.{ Config }
import src.logger.{ Logger }

static VERSION: Str = "1.0.0"
static APP_NAME: Str = "myapp"

fn main() -> Int32 {
    println(APP_NAME + " v" + VERSION + " starting...")

    val cfg = Config.load("config.toml")
    val log = Logger.new(cfg.log_level)

    val server = Server.new(cfg, log)
    val t = thread.spawn(fn server.run)

    println("server running on port " + cfg.port)
    thread.join(t)

    println("shutdown complete")
    return 0
}
```


---

## 14. `implements` dan `contract` [sudah]

> [sudah] Receiver-first method dispatch (`implements Shape`), deklarasi interface (`contract Name`), dan implementasi terikat (`implements Shape with Contract`) selesai end-to-end dengan validasi kelengkapan signature method, pengecekan `Self`, serta penolakan duplikasi.

### `implements` — Method pada Shape

Pengganti `impl` di Rust. Memisahkan data (shape) dan behavior (implements)
tapi tetap di file yang sama.

`self` dipakai sebagai receiver tanpa tipe — compiler inferensi dari konteks.

```
// buffer.cat
shape Buffer {
    data: Vec[UInt8]
}

implements Buffer {
    fn new() -> Buffer {
        return Buffer(data: Vec.new())
    }

    fn push(self: ref mut, val: UInt8) {
        vec.append(self.data, val)
    }

    fn len(self: ref) -> Int32 {
        return vec.len(self.data)
    }

    fn clear(self: ref mut) {
        self.data = Vec.new()
    }

    fn is_empty(self: ref) -> Bool {
        return self.len() == 0
    }
}
```

**Dot notation bekerja normal:**
```
fn main() -> Int32 {
    val buf = Buffer.new()
    buf.push(42)
    buf.push(99)
    println(buf.len())      // 2
    buf.clear()
    println(buf.is_empty()) // true
    return 0
}
```

**Aturan `self`:**
```
fn push(self: ref mut, val: UInt8)   // mutable borrow — boleh ubah self
fn len(self: ref) -> Int32           // immutable borrow — hanya baca
fn consume(self, ...) -> ...         // owned — self dipindah, tidak bisa dipakai lagi
fn new() -> Buffer                   // static method — tidak ada self
```

### `contract` — Pengganti Trait

Mendefinisikan kontrak yang harus dipenuhi sebuah shape.
`Self` (kapital) merujuk ke tipe yang mengimplementasi contract.

```
contract Drawable {
    fn draw(self: ref)
    fn bounds(self: ref) -> Rect
}

contract Serializable {
    fn serialize(self: ref) -> Str
    fn deserialize(data: ref Str) -> Self
}

contract Comparable {
    fn compare(self: ref, other: ref Self) -> Int32
    // -1 = kurang dari, 0 = sama, 1 = lebih dari
}
```

### `implements` dengan `contract`

```
// circle.cat
shape Circle {
    x: Float32
    y: Float32
    radius: Float32
}

implements Circle {
    fn new(x: Float32, y: Float32, r: Float32) -> Circle {
        return Circle(x: x, y: y, radius: r)
    }

    fn area(self: ref) -> Float32 {
        return 3.14159 * self.radius * self.radius
    }
}

// implements with contract — boleh di file berbeda
implements Circle with Drawable {
    fn draw(self: ref) {
        renderer.circle(self.x, self.y, self.radius)
    }

    fn bounds(self: ref) -> Rect {
        return Rect(
            x: self.x - self.radius,
            y: self.y - self.radius,
            w: self.radius * 2,
            h: self.radius * 2
        )
    }
}

implements Circle with Comparable {
    fn compare(self: ref, other: ref Circle) -> Int32 {
        if self.radius < other.radius { return -1 }
        if self.radius > other.radius { return 1 }
        return 0
    }
}
```

### Contract sebagai Tipe Parameter

```
// fungsi yang terima apapun yang implements Drawable
fn render_all(items: ref Vec[ref Drawable]) {
    scan item over items.span() {
        item.draw()
    }
}

// generic dengan contract bound
fn largest[T with Comparable](items: ref Vec[T]) -> ref T {
    var best = ref items.span()[0]
    scan item over items.span() {
        if item.compare(best) > 0 {
            best = ref item
        }
    }
    return best
}
```

### Aturan File

| Jenis | Boleh di file mana |
|---|---|
| `implements Buffer { }` | Harus di file yang sama dengan `shape Buffer` |
| `implements Circle with Drawable { }` | Boleh di file berbeda |

### Error yang Terdeteksi

```
// ERROR 1 — implements di file berbeda dari shape
// src/utils.cat
implements Buffer {              // ERROR — Buffer ada di src/buffer.cat
    fn extra(self: ref) { ... }
}

// ERROR 2 — contract tidak dipenuhi lengkap
implements Circle with Drawable {
    fn draw(self: ref) { ... }
    // ERROR — fn bounds tidak diimplementasi
}

// ERROR 3 — shape tidak implements contract yang dibutuhkan
fn render(item: ref Drawable) { ... }

val p = Point(x: 0, y: 0)
render(ref p)    // ERROR — Point tidak implements Drawable

// ERROR 4 — self ownership salah
implements Buffer {
    fn peek(self: ref) -> UInt8 {
        self.data = Vec.new()    // ERROR — self hanya ref, tidak bisa mutasi
    }
}

// ERROR 5 — implements contract yang sama dua kali
implements Circle with Drawable { ... }
implements Circle with Drawable { ... }    // ERROR — duplikat
```

### Contoh Lengkap — Shape + Contract + Generic

```
contract Summary {
    fn summarize(self: ref) -> Str
}

shape Article {
    share title: Str
    share content: Str
    share author: Str
}

implements Article {
    fn new(title: Str, content: Str, author: Str) -> Article {
        return Article(title: title, content: content, author: author)
    }
}

implements Article with Summary {
    fn summarize(self: ref) -> Str {
        return self.author + ": " + self.title
    }
}

fn notify[T with Summary](item: ref T) {
    println("Breaking news! " + item.summarize())
}

fn main() -> Int32 {
    val article = Article.new(
        "C@ Language",
        "A new systems language...",
        "Alice"
    )
    notify(ref article)
    // output: "Breaking news! Alice: C@ Language"
    return 0
}
```


---

## 15. Operator Overloading [sudah]

> [sudah] Kontrak operator (`Add[T]`, `Sub[T]`, `Equal`, `Order`, dll.) kini diterjemahkan secara natif. Sema mendeteksi tipe `Shape` dan OIR menurunkan ekspresi `v1 + v2` menjadi panggilan method contract (`Shape.add(v1, v2)`). Test end-to-end chaining operator (`(v1 + v2) + v5`) pass secara mulus di native code.

Operator overloading dilakukan lewat `contract` — tidak ada magic syntax terpisah.

```
contract Add[T] {
    fn add(self: ref, other: ref T) -> T
}

contract Equal {
    fn equal(self: ref, other: ref Self) -> Bool
}

contract Order {
    fn compare(self: ref, other: ref Self) -> Int32
    // -1 = kurang dari, 0 = sama, 1 = lebih dari
}

implements Vector2 with Add[Vector2] {
    fn add(self: ref, other: ref Vector2) -> Vector2 {
        return Vector2(x: self.x + other.x, y: self.y + other.y)
    }
}

implements Vector2 with Equal {
    fn equal(self: ref, other: ref Vector2) -> Bool {
        return self.x == other.x and self.y == other.y
    }
}

// Setelah implements, operator bekerja langsung
val v1 = Vector2(x: 1.0, y: 2.0)
val v2 = Vector2(x: 3.0, y: 4.0)
val v3 = v1 + v2       // memanggil add()
val eq = v1 == v2      // memanggil equal()
```

### Contract Operator Lengkap

| Contract | Operator | Method wajib |
|---|---|---|
| `Add[T]` | `+` | `fn add(self: ref, other: ref T) -> T` |
| `Sub[T]` | `-` | `fn sub(self: ref, other: ref T) -> T` |
| `Mul[T]` | `*` | `fn mul(self: ref, other: ref T) -> T` |
| `Div[T]` | `/` | `fn div(self: ref, other: ref T) -> T` |
| `Equal` | `==` `!=` | `fn equal(self: ref, other: ref Self) -> Bool` |
| `Order` | `<` `>` `<=` `>=` | `fn compare(self: ref, other: ref Self) -> Int32` |
| `Negate` | `-x` unary | `fn negate(self: ref) -> Self` |

**Yang tidak boleh di-overload:** `and`, `or`, `not` — boolean logic harus tetap boolean.

### Error yang Terdeteksi
```
// ERROR — pakai operator yang belum diimplementasi
val v1 = Vector2(x: 1.0, y: 2.0)
val v2 = Vector2(x: 3.0, y: 4.0)
val v3 = v1 * v2    // ERROR — Vector2 belum implements Mul
```

---

## 16. String Interpolation

Pakai prefix `f"..."` — eksplisit, tidak ada magic tersembunyi.

```
val name = "Alice"
val age = 30
val score = 98.5

// Interpolation dasar
val msg = f"Hello {name}, age {age}"

// Ekspresi di dalam {}
val info = f"Total: {price * quantity}, items: {items.len()}"

// Panggil method
val upper = f"Name: {name.to_upper()}"

// Multiline
val html = f"
    <h1>{title}</h1>
    <p>{content}</p>
"
```

**Tanpa interpolation — `+` tetap valid:**
```
val msg = "Hello " + name + "!"
```

### Error yang Terdeteksi
```
val msg = f"value: {undefined_var}"    // ERROR — variabel tidak ada
val msg = f"result: {1 + }"           // ERROR — ekspresi tidak valid
val msg = f"no interpolation"         // WARNING — f"" tanpa {} tidak perlu prefix f
```

---

## 17. Anonymous Function

V1 mendukung anonymous function **tanpa capture** dari outer scope.
Closure dengan capture di-defer ke v2.

```
// Anonymous function
val double = fn(x: Int32) -> Int32 { return x * 2 }

// Pass ke fungsi lain
val numbers = Vec[Int32][1, 2, 3, 4, 5]
val doubled = numbers.map(fn(x: Int32) -> Int32 { return x * 2 })
val evens  = numbers.filter(fn(x: Int32) -> Bool { return x % 2 == 0 })

// Thread spawn
val t = thread.spawn(fn() {
    println("running in thread")
})

// Sebagai parameter tipe
fn apply(data: ref Vec[Int32], f: fn(Int32) -> Int32) -> Vec[Int32] {
    return data.map(f)
}
```

**Defer ke v2 — closure dengan capture:**
```
val multiplier = 3
val triple = fn(x: Int32) -> Int32 { return x * multiplier }  // ERROR di v1
// capture dari outer scope belum didukung
```

**Alasan defer:** Capture berinteraksi kompleks dengan ownership system —
perlu desain hati-hati. Anonymous function tanpa capture sudah cukup untuk 80% use case v1.

---

## 18. Casting dan Konversi Tipe

### `as` — Casting Numerik Eksplisit

```
val x: Int32 = 42
val y: Int64  = x as Int64     // widening — selalu aman
val z: Int32  = y as Int32     // narrowing — bisa truncate, tapi eksplisit

val f: Float64 = 3.99
val i: Int32   = f as Int32    // i = 3 — truncate ke zero

val n: Int32   = 100
val d: Float64 = n as Float64  // d = 100.0
```

### Konversi Tipe Tidak Related — Lewat Constructor

```
val s  = Str.from(42)               // Int ke Str
val s2 = Str.from(3.14)             // Float ke Str
val n  = Int32.parse("42")          // Str ke Int — Result[Int32, ParseFault]
val b  = x != 0                     // Int ke Bool — ekspresi biasa
```

### Aturan Casting

| Dari | Ke | Cara |
|---|---|---|
| `Int*` kecil | `Int*` besar | `as` — selalu aman |
| `Int*` besar | `Int*` kecil | `as` — bisa truncate |
| `Int*` | `Float*` | `as` — bisa loss presisi |
| `Float*` | `Int*` | `as` — truncate ke zero |
| `Int*` / `Float*` | `Str` | `Str.from(x)` |
| `Str` | `Int*` / `Float*` | `Int32.parse(s)` → `Result` |

### Error yang Terdeteksi
```
val s: Str  = 42 as Str      // ERROR — pakai Str.from(42)
val b: Bool = 1 as Bool      // ERROR — pakai x != 0
val x: Buffer = y as Buffer  // ERROR — tidak bisa cast antar shape
```

---

## 19. Defect / Panic Behavior

C@ punya dua failure class:
- **Recoverable failure [sudah]** — `Result[T, E]`, harus di-handle eksplisit
- **Defect** — program invariant dilanggar, tidak bisa di-recover

> [sudah] `Result[T, E]` dan `Maybe[T]` sekarang must-use di compiler. Menulisnya sebagai statement lalu membiarkannya hilang akan ditolak; programmer harus menangani, mem-bind, atau membuangnya secara eksplisit.

### Kapan Defect Terjadi
```
// Bounds check
val arr = [1, 2, 3]
val x = arr[99]            // DEFECT — index out of bounds

// Integer overflow pada Int*
val x: Int32 = Int32.MAX
val y = x + 1              // DEFECT — overflow

// Assertion gagal
assert(x > 0, "x harus positif")

// Unwrap None
val val: Maybe[Int32] = None
val n = val.unwrap()       // DEFECT — unwrap pada None
```

### Dua Mode — Dikonfigurasi di `claw.toml`
```
[build]
defect_mode = "abort"    // default — langsung berhenti, print error + stack trace
defect_mode = "unwind"   // stack di-unwind, destructor dipanggil, cocok untuk testing
```

**`abort` mode** — cocok untuk production. Program berhenti bersih, tidak ada partial state korup.

**`unwind` mode** — cocok untuk testing. Bisa catch defect dan lanjut eksekusi test berikutnya.

### Bukan Defect — Gunakan Result
```
// Ini bukan defect — ini recoverable, harus pakai Result
val file = try fs.open("missing.txt") else err {
    return Fail(err)
}
```

---

## 20. Komentar

```
// Single line comment

/*
    Multi line comment
    boleh span beberapa baris
    /* boleh nested */
*/

/// Doc comment — untuk dokumentasi public API
/// Supports markdown
///
/// # Contoh
/// ```
/// val x = add(1, 2)
/// ```
share fn add(a: Int32, b: Int32) -> Int32 {
    return a + b
}
```

**Aturan:**
- `//` — single line, boleh di mana saja
- `/* */` — multi line, boleh nested
- `///` — doc comment, hanya boleh di atas deklarasi `share`

### Error yang Terdeteksi
```
/// Doc comment di non-share item
fn internal() { ... }    // WARNING — /// hanya untuk share items
```


---

## 21. Numeric Literals

```
// Underscore separator — readability
val x = 1_000_000
val y = 3_141_592

// Hex literal
val color = 0xFF8800
val mask: UInt32 = 0xFFFF_0000

// Binary literal
val flags: UInt8 = 0b1010_1010
val bits = 0b0000_1111

// Octal literal
val perm = 0o755
val mode = 0o644

// Semua bisa dikombinasi dengan tipe eksplisit
val x: UInt32 = 0xFF
val y: Int32  = 1_000_000
```

**Aturan:**
- `0x` prefix — hex, huruf A-F case-insensitive
- `0b` prefix — binary
- `0o` prefix — octal
- `_` boleh di mana saja dalam angka kecuali di awal — hanya untuk readability, tidak mempengaruhi nilai

**Error yang terdeteksi:**
```
val x = _1000     // ERROR — underscore di awal
val y = 0x        // ERROR — hex tanpa digit
val z = 0b2       // ERROR — binary hanya 0 dan 1
val w = 0o9       // ERROR — octal hanya 0-7
```

---

## 22. Equality vs Identity

```
// == memanggil Equal contract — value equality
val a = Config(host: "localhost", port: 8080)
val b = Config(host: "localhost", port: 8080)
a == b    // true — nilai sama

// is — pointer/identity equality — apakah object yang sama di memori?
val r1 = ref a
val r2 = ref a
val r3 = ref b
r1 is r2    // true — sama object
r1 is r3    // false — object berbeda walaupun nilai sama
```

**Aturan:**
- `==` dan `!=` hanya bekerja kalau tipe implements `Equal` contract
- `is` hanya bekerja pada `ref` — tidak ada identity check pada owned value
- `is` tidak bisa di-overload — selalu pointer comparison

**Error yang terdeteksi:**
```
val a = Config(...)
val b = Config(...)
a is b    // ERROR — is hanya untuk ref, bukan owned value
```

---

## 23. UInt Overflow Behavior

```
// Int* — overflow = DEFECT
val x: Int32 = Int32.MAX
val y = x + 1    // DEFECT — overflow pada signed integer

// UInt* — overflow = wrapping (silent, defined behavior)
val x: UInt32 = UInt32.MAX
val y = x + 1    // y = 0 — wrapping, bukan DEFECT

// UInt wrapping berguna untuk bit manipulation
val mask: UInt8 = 0xFF
val next = mask + 1    // next = 0 — wrapping expected
```

**Alasan:**
`UInt` sering dipakai untuk bit manipulation, checksum, hash, dan protocol fields
di mana wrapping adalah behavior yang diharapkan.
`Int` untuk arithmetic biasa di mana overflow adalah bug.

---

## 24. Zero Value / Default Initialization — Hybrid

**Primitif punya zero value default, shape wajib eksplisit.**

```
// Primitif — boleh tanpa inisialisasi, default ke zero
var x: Int32      // x = 0
var y: Float64    // y = 0.0
var b: Bool       // b = false
var s: Str        // s = ""
var p: UInt32     // p = 0

// Tapi lebih baik eksplisit untuk clarity
var x: Int32 = 0  // lebih jelas

// Shape — WAJIB inisialisasi eksplisit
var cfg: Config              // ERROR — shape tidak punya zero value
var cfg = Config.new()       // OK
var cfg = Config(
    host: "localhost",
    port: 8080
)                            // OK
```

**Alasan:**
- Primitif zero value aman karena maknanya universal (0, false, "")
- Shape zero value berbahaya — field `Config(host: "", port: 0)` bisa jadi state invalid
- Programmer tahu lebih baik dari compiler apa "default" yang valid untuk shape mereka

**Koleksi mengikuti primitif:**
```
var nums: Vec[Int32]     // nums = Vec kosong — OK
var map: Map[Str, Int32] // map = Map kosong — OK
var arr: Array[Int32, 5] // arr = [0, 0, 0, 0, 0] — OK
```

---

## 25. Destructuring

```
// Tuple destructuring
fn min_max(data: ref Span[Int32]) -> (Int32, Int32) {
    return (0, 100)    // placeholder
}

val (min, max) = min_max(data.span())
println(f"min: {min}, max: {max}")

// Destructuring di val/var
val (x, y) = (10, 20)
var (a, b) = (1, 2)

// Partial destructuring — pakai _ untuk skip
val (first, _) = min_max(data.span())

// Destructuring di pick pattern
pick result {
    Ok((a, b)) {
        println(f"got {a} and {b}")
    }
    Fail(e) { ... }
}

// Nested destructuring
val ((x1, y1), (x2, y2)) = get_line()
```

**Error yang terdeteksi:**
```
val (a, b, c) = (1, 2)    // ERROR — jumlah field tidak cocok
val (x, x) = (1, 2)       // ERROR — nama duplikat dalam destructuring
```

---

## 26. Range Syntax

```
// Exclusive — tidak termasuk angka akhir
scan i over 0..10 {
    println(i)    // 0, 1, 2, ... 9
}

// Inclusive — termasuk angka akhir
scan i over 0..=10 {
    println(i)    // 0, 1, 2, ... 10
}

// Range dengan variable
val start = 5
val end = 15
scan i over start..end { ... }      // exclusive
scan i over start..=end { ... }     // inclusive

// Range sebagai nilai
val r = 0..10                       // Range[Int32]
val items = Vec[Int32][1,2,3,4,5]
val subset = items.span()[2..4]     // slice dengan range

// Range di kondisi
val r = 0..100
if x in r { ... }                   // apakah x dalam range?
```

**Aturan:**
- `..` — exclusive range (seperti Python `range(a, b)`)
- `..=` — inclusive range
- Range hanya untuk tipe yang implements `Order` contract
- Range bisa dipakai sebagai slice index pada `Span` dan `Array`

---

## 27. `scan` dengan Index

```
// Tanpa index
scan item over items {
    println(item)
}

// Dengan index — pakai comma
scan item, i over items {
    println(f"{i}: {item}")
}

// Dengan range
scan i over 0..10 {
    println(i)
}

// Kombinasi
scan item, i over items.span()[0..5] {
    println(f"[{i}] = {item}")
}
```

**Aturan:**
- `item` adalah nilai per iterasi
- `i` adalah index — selalu bertipe `USize`
- Order wajib: nilai dulu, index kedua — `scan item, i` bukan `scan i, item`

---

## 28. Method Chaining

```
// Chaining multi-line — lebih readable
val result = numbers
    .filter(fn(x: Int32) -> Bool { return x > 0 })
    .map(fn(x: Int32) -> Int32 { return x * 2 })
    .span()

// Chaining single line
val total = numbers.filter(fn(x) { return x > 0 }).map(fn(x) { return x * 2 })

// Chaining dengan intermediate variable — boleh mix
val positives = numbers.filter(fn(x: Int32) -> Bool { return x > 0 })
val doubled = positives.map(fn(x: Int32) -> Int32 { return x * 2 })

// Chaining dengan Result
val result = try parse(input)
    .validate()
    .transform() else err {
        return Fail(err)
    }
```

**Aturan:**
- Method chaining bekerja karena setiap method return tipe baru
- Compiler inferensi tipe di setiap step
- Multiline chaining: titik di awal baris berikutnya
- Chaining tidak mengubah ownership rules — masih berlaku normal

---

## 29. Borrow System  Formal Rules [sudah]

### Path-Based Borrow Region

```
// Borrow region = path dari root ke field
cfg              // seluruh Config
cfg.db           // sub-struct db
cfg.db.host      // field host dalam db
cfg.db.port      // field port dalam db
```

### Overlap Rules

```
// A overlap B jika:
// - A == B (sama persis)
// - A adalah prefix dari B
// - B adalah prefix dari A

cfg.db      overlap cfg.db.host   // ✅ — cfg.db adalah prefix
cfg.db.host overlap cfg.db.port   // ❌ — tidak ada prefix relation
cfg         overlap cfg.db.host   // ✅ — cfg adalah prefix
```

### Conflict Rules

```
// Conflict jika: region overlap DAN salah satu mutable

val a = ref cfg.db.host       // immutable
val b = ref cfg.db.port       // immutable — OK, tidak overlap

val a = ref cfg.db.host       // immutable
val b = ref mut cfg.db.port   // mutable — OK, tidak overlap

val a = ref cfg.db            // immutable — mencakup db.*
val b = ref mut cfg.db.port   // ERROR — cfg.db overlap cfg.db.port

val a = ref mut cfg.db.host   // mutable
val b = ref mut cfg.db.host   // ERROR — sama path, keduanya mutable
```

### Alias Tracking — Memory Origin

```
// val x = cfg — ini MOVE, bukan alias (owned type)
var cfg = Config(host: "localhost", port: 8080)
val x = cfg          // cfg dipindah ke x — cfg tidak valid lagi
val h = ref x.host   // OK — borrow dari x

// ref adalah alias
val r1 = ref cfg
val r2 = ref cfg     // r1 dan r2 ke origin yang sama
                     // compiler track keduanya ke cfg
```

### Reborrow

```
val a: ref Config = ref cfg
val b: ref Str = ref a.host    // reborrow — OK, immutable
// ref (ref T) = ref T — selalu immutable
```

### Drop saat Borrow Aktif

```
var cfg = Config(host: "localhost", port: 8080)
val h = ref cfg.host

cfg = Config(host: "new", port: 9090)  // ERROR — cfg.host sedang dipinjam
                                        // drop cfg lama tidak boleh

// Solusi: drop borrow dulu
{
    val h = ref cfg.host
    println(h)
}   // h selesai di sini
cfg = Config(host: "new", port: 9090)  // OK — tidak ada borrow aktif
```

### Borrow dalam `pick`

```
var result: Result[Config, Fault] = ...

pick result {
    Ok(cfg) {
        val h = ref cfg.host    // OK — dalam branch ini
        println(h)
    }   // h tidak valid di luar blok ini
    Fail(e) { ... }
}
// h tidak accessible di sini
```

### Borrow dalam `try`

```
fn load(cfg: ref Config) -> Result[Data, Fault] {
    val h = ref cfg.host         // borrow cfg.host
    val data = try fetch(h) else err {
        // h masih valid di dalam else block
        println(h)
        return Fail(err)
    }
    // h masih valid di sini — sama scope
    return Ok(data)
}
```

### Vec — Wajib lewat Span

```
var nums = Vec[Int32][1, 2, 3]

// TIDAK BOLEH — borrow langsung ke Vec element
val first = ref nums[0]   // ERROR — Vec bisa reallocate

// BOLEH — borrow lewat Span
val span = nums.span()
val first = ref span[0]   // OK — Span tidak bisa reallocate
val second = ref span[1]  // OK — path berbeda

// Mutasi Vec tidak boleh saat ada Span aktif
val span = nums.span()
nums.push(4)              // ERROR — ada Span aktif
```

### Ringkasan Aturan

| Situasi | Boleh? |
|---|---|
| `ref field_A` + `ref field_B` berbeda | ✅ |
| `ref field_A` + `ref mut field_B` berbeda | ✅ |
| `ref mut field_A` + `ref mut field_B` berbeda | ✅ |
| `ref struct` + `ref field` (overlap) | ❌ |
| `ref field` + `ref mut field` sama | ❌ |
| `ref mut field` + `ref mut field` sama | ❌ |
| Reborrow `ref (ref T)` | ✅ immutable saja |
| Drop saat borrow aktif | ❌ |
| Borrow Vec element langsung | ❌ pakai Span |
| Borrow Span element | ✅ |
| Mutasi Vec saat Span aktif | ❌ |


---

## 30. Toolchain — `claw` dan `paw`

### Pemisahan Peran

| Tool | Peran |
|---|---|
| `claw` | Compiler — build, check, emit IR |
| `paw` | Package manager — add, remove, update, audit |

`paw` adalah wrapper di atas `claw` untuk workflow sehari-hari.

### `claw.toml` — Config Workspace

File konfigurasi utama yang dideteksi compiler. Bukan `paw.pkg`.

```toml
# claw.toml
[project]
name = "myapp"
version = "0.1.0"
edition = "2025"

[build]
defect_mode = "abort"    # abort | unwind

[dependencies]
term = "1.0.0"
http = { version = "2.0.0", features = ["tls"] }
```

### Perintah `claw`

```
claw build          # compile project
claw check          # type check tanpa compile
claw emit-ir        # emit intermediate representation
claw emit-llvm      # emit LLVM IR
```

### Perintah `paw`

```
paw add http        # tambah dependency
paw remove http     # hapus dependency
paw update          # update semua dependency
paw audit           # cek security advisory
paw run             # build + run
paw test            # run tests
paw fmt             # format kode
paw doc             # generate docs
```

---

## 31. Built-in Method Dispatch

Semua built-in types punya method dispatch langsung — tidak perlu prefix namespace.
Built-in methods otomatis public (tidak perlu `share`).

### `Vec[T]`

```
val numbers = Vec[Int32][1, 2, 3, 4, 5]

numbers.len() -> USize
numbers.is_empty() -> Bool
numbers.push(val: T)                          // mutasi
numbers.pop() -> Maybe[T]                     // mutasi
numbers.get(i: USize) -> Maybe[ref T]
numbers.span() -> Span[T]
numbers.clear()                               // mutasi
numbers.contains(val: ref T) -> Bool          // butuh Equal
numbers.map(fn(T) -> U) -> Vec[U]            // return baru
numbers.filter(fn(T) -> Bool) -> Vec[T]      // return baru
numbers.first() -> Maybe[ref T]
numbers.last() -> Maybe[ref T]

// Contoh
val doubled = numbers.map(fn(x) { x * 2 })
val positives = numbers.filter(fn(x) { x > 0 })
val total = numbers.len()
```

### `Str`

```
val text = "Hello, World!"

text.len() -> USize
text.is_empty() -> Bool
text.contains(s: ref Str) -> Bool
text.starts_with(s: ref Str) -> Bool
text.ends_with(s: ref Str) -> Bool
text.to_upper() -> Str
text.to_lower() -> Str
text.trim() -> Str
text.trim_start() -> Str
text.trim_end() -> Str
text.split(sep: ref Str) -> Vec[Str]
text.replace(from: ref Str, to: ref Str) -> Str
text.find(s: ref Str) -> Maybe[USize]
text.slice(range) -> Str

// Contoh
val upper = text.to_upper()
val words = text.split(" ")
val clean = text.trim()
```

### `Map[K, V]`

```
val table = Map[Str, Int32].new()

table.get(key: ref K) -> Maybe[ref V]
table.set(key: K, val: V)                    // mutasi
table.remove(key: ref K) -> Maybe[V]         // mutasi
table.contains_key(key: ref K) -> Bool
table.len() -> USize
table.is_empty() -> Bool
table.keys() -> Vec[ref K]
table.values() -> Vec[ref V]
table.clear()                                // mutasi

// Contoh
table.set("alice", 30)
val age = table.get("alice")    // Maybe[ref Int32]
```

### `Span[T]`

```
val span = numbers.span()

span.len() -> USize
span.is_empty() -> Bool
span.get(i: USize) -> Maybe[ref T]
span.first() -> Maybe[ref T]
span.last() -> Maybe[ref T]
span.contains(val: ref T) -> Bool
span[range] -> Span[T]
span.map(fn(T) -> U) -> Vec[U]
span.filter(fn(T) -> Bool) -> Vec[T]

// Contoh
val subset = span[0..3]
val doubled = span.map(fn(x) { x * 2 })
```

### `Array[T]`

```
val arr: Array[Int32, 5] = [1, 2, 3, 4, 5]

arr.len() -> USize           // selalu N — compile time constant
arr.span() -> Span[T]
arr.get(i: USize) -> Maybe[ref T]
arr.contains(val: ref T) -> Bool
```

---

## 32. Anonymous Fn — Type Inference

Anonymous function mendukung **inferensi tipe parameter dan return**.
Tipe diinferensi dari konteks penggunaan.

```
// Tipe diinferensi dari Vec[Int32]
val numbers = Vec[Int32][1, 2, 3]
numbers.map(fn(x) { x * 2 })       // x: Int32, return: Int32
numbers.filter(fn(x) { x > 0 })    // x: Int32, return: Bool

// Multi statement — implicit return dari ekspresi terakhir
numbers.map(fn(x) {
    val doubled = x * 2
    if doubled > 100 { 100 } else { doubled }
})

// Early return — tetap pakai return eksplisit
numbers.map(fn(x) {
    if x < 0 { return 0 }
    x * 2
})
```

**Named fn tetap wajib tipe eksplisit:**
```
// Named fn — tipe wajib, return opsional
fn double(x: Int32) -> Int32 {
    x * 2    // implicit return OK
}

// Anonymous fn — tipe inferensi
val f = fn(x) { x * 2 }
```

---

## 33. Implicit Return [sudah]

Berlaku untuk **semua fn** — named maupun anonymous.
Ekspresi terakhir di blok otomatis jadi return value.

```
// Named fn
fn add(a: Int32, b: Int32) -> Int32 {
    a + b
}

fn clamp(val: Int32, min: Int32, max: Int32) -> Int32 {
    if val < min { return min }    // early return
    if val > max { return max }    // early return
    val                            // implicit return
}

// Anonymous fn
numbers.map(fn(x) { x * 2 })

// Dengan logika kompleks
fn describe(score: Int32) -> Str {
    if score >= 90 { "A" }
    else if score >= 70 { "B" }
    else { "C" }
}
```

**Aturan:**
- Ekspresi terakhir di blok = implicit return
- Kalau ada statement setelah ekspresi = wajib `return` eksplisit
- Early exit tetap pakai `return` eksplisit
- `Unit` fn tidak perlu return apapun

```
// ERROR — statement setelah ekspresi terakhir ambigu
fn bad(a: Int32) -> Int32 {
    a * 2
    println("done")    // ERROR — mana yang jadi return value?
}

// BENAR
fn good(a: Int32) -> Int32 {
    val result = a * 2
    println("done")
    return result       // eksplisit karena ada statement setelahnya
}
```

---

## 34. `if` dan `pick` sebagai Expression

`if` dan `pick` bisa dipakai sebagai expression — menghasilkan nilai.

### `if` Expression

```
// if sebagai expression
val label = if score >= 90 { "A" } else { "B" }

// Nested
val grade = if score >= 90 { "A" }
            else if score >= 70 { "B" }
            else if score >= 50 { "C" }
            else { "F" }

// Di dalam fungsi
fn abs(x: Int32) -> Int32 {
    if x < 0 { -x } else { x }
}

// Di dalam fungsi lain
val result = numbers.map(fn(x) {
    if x < 0 { 0 } else { x }
})
```

**Aturan `if` expression:**
- Semua branch harus return tipe yang sama
- `else` wajib ada kalau dipakai sebagai expression
- Kalau `else` tidak ada — hanya boleh sebagai statement

```
// ERROR — tipe tidak konsisten
val x = if condition { 42 } else { "hello" }    // ERROR — Int32 vs Str

// ERROR — else tidak ada saat dipakai sebagai expression
val x = if condition { 42 }    // ERROR — kemana nilai kalau false?

// OK — tanpa else kalau sebagai statement
if condition {
    println("yes")
}
```

### `pick` Expression

```
// pick sebagai expression
val label = pick maybe {
    None { "empty" }
    Some(v) { f"value: {v}" }
}

// Di dalam fungsi
fn describe(result: Result[Int32, Str]) -> Str {
    pick result {
        Ok(n)  { f"success: {n}" }
        Fail(e) { f"error: {e}" }
    }
}

// Chaining dengan method
val message = pick table.get("key") {
    None    { "not found" }
    Some(v) { f"found: {v}" }
}
```

**Aturan `pick` expression:**
- Semua branch harus return tipe yang sama
- Exhaustiveness tetap wajib
- Bisa dipakai langsung sebagai argument fungsi

```
println(pick status {
    Ok(v)   { f"ok: {v}" }
    Fail(e) { f"fail: {e}" }
})
```


---

## 35. Bitwise Operators

```
val a: UInt32 = 0b1010_1010
val b: UInt32 = 0b1111_0000

// Bitwise operators
val and_result = a & b      // AND  → 0b1010_0000
val or_result  = a | b      // OR   → 0b1111_1010
val xor_result = a ^ b      // XOR  → 0b0101_1010
val not_result = ~a         // NOT  → complement
val shl_result = a << 2     // shift left
val shr_result = a >> 2     // shift right
```

**Aturan:**
- Bitwise hanya untuk tipe `UInt*` — tidak untuk `Int*` yang signed
- Hasil tipe mengikuti operand
- Shift amount harus `USize`

**Error yang terdeteksi:**
```
val x: Int32 = 42
val y = x & 0xFF    // ERROR — bitwise hanya untuk UInt*
val z = a << -1     // ERROR — shift amount tidak boleh negatif
```

---

## 36. Compound Assignment

```
var x = 10

// Arithmetic
x += 5      // x = x + 5  → 15
x -= 3      // x = x - 3  → 12
x *= 2      // x = x * 2  → 24
x /= 4      // x = x / 4  → 6
x %= 4      // x = x % 4  → 2

// Bitwise compound — hanya UInt*
var flags: UInt32 = 0xFF00
flags &= 0x0F0F     // AND assign
flags |= 0b1010     // OR assign
flags ^= 0xFF       // XOR assign
flags <<= 2         // shift left assign
flags >>= 1         // shift right assign

// String concatenation assign
var msg = "Hello"
msg += ", World!"   // msg = "Hello, World!"
```

**Aturan:**
- Semua compound assignment hanya valid pada `var` binding
- `val` binding tidak bisa compound assign

```
val x = 10
x += 5    // ERROR — val tidak bisa diubah
```

---

## 37. `rewrite` — Metaprogramming

`rewrite` adalah sistem metaprogramming C@ yang bisa akses AST.
Dipanggil dengan `!` suffix untuk membedakan dari fungsi biasa.

### Deklarasi

```
rewrite log(msg) {
    println(f"[{@file}:{@line}] {msg}")
}

rewrite assert_eq(a, b) {
    if a != b {
        println(f"assertion failed: {@src(a)} != {@src(b)}")
        println(f"  left:  {a}")
        println(f"  right: {b}")
        defect()
    }
}

rewrite dbg(expr) {
    val result = expr
    println(f"[dbg] {@src(expr)} = {result}")
    result    // implicit return — dbg! menghasilkan nilai
}

rewrite swap(a, b) {
    val __tmp = a
    a = b
    b = __tmp
}

rewrite bench(name, body) {
    val __start = time.now()
    body
    val __elapsed = time.now() - __start
    println(f"[bench] {name}: {__elapsed}ms")
}
```

### Penggunaan

```
// Dipanggil dengan !
log!("server started")
assert_eq!(result, 42)
swap!(x, y)

val value = dbg!(expensive_fn())

bench!("sort", {
    numbers.sort()
})
```

### Built-in AST Variables

| Variable | Isi |
|---|---|
| `@file` | Nama file saat ini sebagai `Str` |
| `@line` | Nomor baris saat ini sebagai `USize` |
| `@src(expr)` | Source code dari expression sebagai `Str` |
| `@type(expr)` | Nama tipe dari expression sebagai `Str` |
| `@expand` | Full expansion untuk debugging |

```
rewrite type_info(expr) {
    println(f"{@src(expr)} bertipe {@type(expr)}")
    expr
}

type_info!(user.age)
// output: "user.age bertipe Int32"
```

### Hygiene — Tidak Ada Variable Leakage

```
rewrite swap(a, b) {
    val __tmp = a    // __tmp tidak bocor ke scope caller
    a = b
    b = __tmp
}

var tmp = 999        // tidak terpengaruh
swap!(x, y)
println(tmp)         // tetap 999
```

### Constraint

```
// TIDAK BOLEH — rewrite rekursif
rewrite bad(x) {
    bad!(x)    // ERROR — infinite expansion
}

// TIDAK BOLEH — rewrite di dalam raw {}
raw {
    log!("test")    // ERROR
}

// BOLEH — rewrite memanggil rewrite lain
rewrite outer(x) {
    inner!(x + 1)
}
```

---

## 38. Annotation System

Annotation memberikan metadata pada deklarasi.
Diproses saat compile time.

### Syntax

```
@annotation_name
@annotation_name(arg1, arg2)
@annotation_name("string arg")
```

### Built-in Annotations

| Annotation | Target | Fungsi |
|---|---|---|
| `@derive(X, Y)` | `shape`, `choice` | Auto-generate implements |
| `@inline` | `fn` | Hint compiler untuk inline |
| `@test` | `fn` | Mark sebagai test function |
| `@deprecated(msg)` | semua | Warning saat dipakai |
| `@repr(C)` | `shape` | C-compatible memory layout |
| `@skip` | field | Skip saat derive serialization |
| `@sealed` | `contract` | Tidak boleh diimplementasi di luar module |
| `@extern(abi)` | `fn`, `implements` | FFI boundary |
| `@allow(warning)` | semua | Suppress warning tertentu |

### `@derive` — Paling Powerful

Compiler auto-generate `implements` untuk contract yang diminta:

```
@derive(Debug, Clone, Equal, Serialize)
shape User {
    share name: Str
    share age: Int32
    @skip                      // skip dari Debug dan Serialize
    share password_hash: Str
}

// Compiler auto-generate semua ini:
implements User with Debug {
    fn debug(self: ref) -> Str {
        f"User {{ name: {self.name}, age: {self.age} }}"
    }
}

implements User with Clone {
    fn clone(self: ref) -> User {
        User(name: self.name.clone(), age: self.age)
    }
}

implements User with Equal {
    fn equal(self: ref, other: ref User) -> Bool {
        self.name == other.name and self.age == other.age
    }
}
```

### Contoh Annotation Lainnya

```
// @test — untuk unit testing
@test
fn test_add() {
    assert_eq!(add(1, 2), 3)
    assert_eq!(add(-1, 1), 0)
}

// @inline — hint optimasi
@inline
fn fast_abs(x: Int32) -> Int32 {
    if x < 0 { -x } else { x }
}

// @deprecated
@deprecated("use parse_v2() instead")
fn parse(text: ref Str) -> Result[Int32, ParseFault] { ... }

// @repr(C) — FFI compatible layout
@repr(C)
shape CPoint {
    share x: Float32
    share y: Float32
}

// @sealed — tidak bisa diimplementasi di luar module
@sealed
contract InternalProtocol {
    fn process(self: ref) -> Result[Unit, Fault]
}
```

### User-Defined Annotation lewat `rewrite`

```
rewrite annotation retry(times: Int32) on fn {
    var __attempts = 0
    loop {
        val __result = try @target() else err {
            __attempts += 1
            if __attempts >= times { return Fail(err) }
            skip
        }
        return Ok(__result)
    }
}

// Penggunaan
@retry(3)
fn fetch(url: ref Str) -> Result[Data, NetFault] {
    ...
}
```

`on fn` = annotation ini hanya valid di atas fungsi.

### Error yang Terdeteksi

```
// ERROR — annotation di tempat yang salah
@test
shape Config { ... }    // ERROR — @test hanya untuk fn

// ERROR — @derive contract yang tidak valid
@derive(NonExistent)
shape Point { ... }     // ERROR — NonExistent bukan contract

// ERROR — @sealed dilanggar
@sealed
contract Internal { ... }

// src/other.cat
implements MyType with Internal { ... }    // ERROR — sealed
```






