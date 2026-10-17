# C@ — Paket Solusi untuk Seluruh Temuan Proposal PRD

**Dasar:** `Proposal_PRD___Sintaks_C__yang_Janggal...md` (15 temuan, 19 keputusan terbuka), `cat-language-design-summary_all.md`, dan `cat-language-design-summary.md`. **Slogan acuan:** Fast, Safe, Simple (PRD utama baris 3–4). **Status:** REVISI FINAL. Memuat keputusan terbaru: koleksi `vec[]`/`set()`/`map{}`, dataflow borrow checking, `super` dipertahankan, kebijakan release, aturan huruf kecil, dan `rewrite` di v1.x.

**Ratifikasi Setya — 11 Oktober 2026:** Setya secara eksplisit meratifikasi seluruh keputusan pada tabel ringkasan, termasuk entri yang saat dokumen disusun bertanda 🟦. Simbol 🟦 hanya merekam status penyusunan saat itu, bukan isu terbuka saat ini. Ratifikasi ini tidak mengubah substansi desain maupun batas versi dan staging pada roadmap.

**Cara membaca**

- ✅ = sudah kamu putuskan di percakapan ini.
- 🟦 = status usulan saat dokumen disusun; keputusan tersebut kini telah diratifikasi sebagaimana catatan di atas. Simbol ini bukan status implementasi.
- Label fence menunjukkan dukungan compiler saat ini, bukan status persetujuan desain: `cat proposal` berarti contoh sintaks belum didukung; status keputusan dan staging versi tetap ditentukan oleh teks bagian dan roadmap.
- Nama file/baris compiler dikutip dari proposal; bagian "cara kerja" menjelaskan desain dan bukan bukti implementasi.
- Contoh diagnostik adalah **target** pesan error, bukan output compiler asli.

---

## 0. Pengaman lintas masalah (menutup risiko di semua bagian)

Enam mekanisme ini dipakai berulang. Karena itu risiko tiap bagian di bawah bisa dibilang kecil.

1. **Doc-test.** Fence `cat` wajib lolos `claw check`; `cat error: ...` wajib ditolak dengan diagnostik yang dicantumkan. Fence `cat proposal` dan `text` dilewati serta dihitung secara eksplisit; fence tanpa label atau dengan label tak dikenal menggagalkan gerbang.
2. **Status keputusan dan implementasi terpisah.** Tag doc-test tidak menyatakan bahwa fitur sudah selesai; dukungan compiler dan staging versi harus dibaca dari milestone serta bukti implementasi.
3. **Tes negatif.** Selain program yang harus lolos, ada daftar program yang **harus ditolak** beserta pesan errornya. Untuk slogan Safe, inilah buktinya.
4. **Tabel sebagai satu-satunya sumber kebenaran.** Presedensi operator dan grammar pattern ditulis sebagai tabel, dan parser dibangun dari tabel itu (bukan sebaliknya).
5. **Daftar keputusan tertunda.** Setiap hal yang ditunda punya tiga kolom: keputusan sementara, versi target, dan syarat selesai.
6. **Milestone bertahap** (bagian 18). Fitur besar dipecah supaya tidak ada satu perubahan raksasa yang berisiko.

---

## Ringkasan: 19 keputusan terbuka dan jawabannya

| # | Pertanyaan proposal | Jawaban | Status |
| --- | --- | --- | --- |
| 1 | Bentuk konstruksi shape | `Point { x: 1 }` hanya di modul pendefinisi; dari luar `Point.new(...)` | ✅ |
| 2 | Named arguments fungsi | Tidak ada; argumen posisional | ✅ |
| 3 | Literal koleksi | `[a]` Array, `vec[a]`, `set(a)`, `map{k: v}`; tanpa `.new/.from` | ✅ |
| 4 | Panjang `Array` | `Array[T, N]`, N konstanta saat kompilasi | 🟦 |
| 5 | Operator wajib | Semua yang ada di PRD, dalam 4 grup bertahap; satu tabel presedensi | 🟦 |
| 6 | `if`/`pick` sebagai expression | Ya; `else` wajib; `else if` langsung | 🟦 |
| 7 | Seberapa jauh pattern `pick` | Pattern penuh (varian, `_`, tuple, bersarang), bertahap | 🟦 |
| 8 | Casing varian | Case-sensitive; varian buatan pengguna huruf kecil; `Ok/Fail/Some/None` kapital | ✅ |
| 9 | Bentuk `try` | Hanya sebagai initializer `val`; error type harus identik | 🟦 |
| 10 | Tuple | Satu paket utuh: tipe, literal, `.0`, destructuring, pattern | 🟦 |
| 11 | Range | `Range[T]`, `..` eksklusif, `..=` inklusif, dipakai seragam | 🟦 |
| 12 | `scan item, i` | Ya; `i: USize`, mulai dari 0 | 🟦 |
| 13 | `var x: T` tanpa nilai | Tidak otomatis nol; wajib diisi sebelum dibaca | 🟦 |
| 14 | Klaim lifetime | Borrow check berbasis dataflow + elision + `ref[s]` saat ambigu; `ref mut[s]` ditolak di v1 | ✅ |
| 15 | `with` pada generics dan locality | Pertahankan `with`; aturan orphan | 🟦 |
| 16 | `slice` | Indexing dengan range sebagai satu-satunya bentuk publik | 🟦 |
| 17 | Literal numerik/Char/string/doc | Semua masuk v1, dengan aturan lexer yang tegas | 🟦 |
| 18 | `foreign c`, `rewrite`, annotation | `foreign c` dan annotation bawaan masuk v1; `rewrite` ke v1.x | ✅ |
| 19 | Dua PRD | Satu PRD otoritatif; ringkasan dibuat otomatis | 🟦 |
| + | Penulisan huruf | Kapital hanya untuk nama tipe (+ `Ok/Fail/Some/None`); sisanya huruf kecil | ✅ |
| + | `import super` | Dipertahankan untuk file di folder yang sama | ✅ |
| + | Mode release | Overflow `Int*` membungkus tanpa cek; cek batas indeks tetap; `noalias` | ✅ |

---

## 1. Konstruksi shape dan named arguments ✅

**Aturan**

- Argumen fungsi selalu posisional. Tidak ada label.
- Literal shape `Point { x: 1, y: 2 }` hanya sah di modul tempat shape didefinisikan.
- Dari luar modul, satu-satunya cara adalah `Point.new(...)`.
- `new` otomatis lewat `@constructor` wajib ada sebelum rilis v1 (bagian 15).

```cat proposal
shape Config {
    share host: String
    share port: Int32
}

implements Config {
    share fn new(host: Str, port: Int32) -> Result[Config, String] {
        if port < 1 or port > 65535 { return Fail(String.from("port tidak valid")) }
        return Ok(Config { host: String.from(host), port: port })
    }
}

// modul lain
val c = try Config.new("a", 80) else err { return Fail(err) }
```

**Cara kerja compiler:** parser menerima literal hanya sebagai `Nama { field: expr }` (sudah ada). Pemeriksaan di sema: semua field terisi, tidak ada field ganda, tipe cocok, dan **modul pemanggil sama dengan modul definisi shape**. LLVM: literal menjadi `alloca` lalu `store` per field, atau `insertvalue` untuk nilai kecil.

**Diagnostik target**

```text
error: shape 'Config' tidak boleh dibuat dengan literal di luar modulnya
 --> src/main.cat:4:11
  = bantuan: gunakan Config.new(...)
```

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Boilerplate `new` di v0.x | Dicatat sebagai utang; `@constructor` adalah syarat v1 (bagian 15) |
| Fungsi dengan banyak parameter sulit dibaca tanpa label | (a) lint peringatan jika parameter > 4; (b) language server menampilkan nama parameter sebagai inlay hint; (c) tipe berbeda untuk nilai berbeda, misalnya `Port` bukan `Int32` |
| Idiom "struct opsi" terhalang aturan literal | Dicatat di daftar tertunda: bila (a)–(c) tidak cukup, pertimbangkan modifier `open shape` untuk shape data murni (semua field `share`, tanpa invariant) |
| Contoh PRD lama (`Buffer(data: ...)`, `Circle(x: ...)`, `Vector2(...)`, `Config(host: ...)`, dan `Node(data: 1, next: None)` di bagian Arena) | Ditulis ulang ke `Tipe.new(...)`; doc-test menangkap yang terlewat |

---

## 2. Literal koleksi dan panjang `Array` ✅

**Aturan**

- **Tipe** (huruf kapital): `Array[T, N]` (panjang tetap, N bagian dari tipe), `Span[T]` (view tanpa kepemilikan), `Vec[T]`, `Set[T]`, `Map[K, V]`.
- **Literal** (huruf kecil):

| Literal | Tipe |
| --- | --- |
| `[1, 2, 3]` | `Array[Int32, 3]` |
| `vec[1, 2, 3]` | `Vec[Int32]` |
| `set(1, 2, 3)` | `Set[Int32]` |
| `map{1: 10, 2: 20}` | `Map[Int32, Int32]` |

- `vec`, `set`, `map` adalah kata cadangan, jadi `vec[`, `set(`, `map{` selalu dibaca sebagai literal dan tidak bentrok dengan indexing, pemanggilan fungsi, atau literal shape.
- Literal kosong (`vec[]`, `set()`, `map{}`) butuh tipe yang bisa dikenali dari anotasi, parameter, atau tipe return.
- `.new()` dan `.from()` untuk Vec/Set/Map dihapus dari PRD. Contoh PRD (Buffer, Arena, dst.) ditulis ulang. Koleksi lain seperti `Queue` tetap `Queue.new()` sampai bentuk literalnya ditentukan.

```cat proposal
val a = [1, 2, 3]                       // Array[Int32, 3]
val v = vec[1, 2, 3]                    // Vec[Int32]
val s = set(10, 20)                       // Set[Int32]
val m = map{1: 10, 2: 20}                 // Map[Int32, Int32]
val e: Vec[Int32] = vec[]               // kosong: tipe dari anotasi
val first = v[0]                        // indexing: ada operan kiri
```

**Cara kerja compiler**

- Lexer: `vec`, `set`, `map` menjadi keyword. Parser: `vec [ ... ]`, `set ( ... )`, `map { k : v, ... }`.
- Sema: semua elemen satu tipe; kunci `map` dan elemen `set` harus punya kontrak hash dan kesamaan; literal `Array` meng-infer panjang dari jumlah elemen.
- Argumen tipe boleh berupa literal integer (`Array[UInt8, 4]`).
- LLVM: `Array` menjadi `[N x T]`. `Vec` mengalokasikan heap sekali sebesar jumlah elemen lalu membentuk `{ ptr, len, cap }`.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| `vec`/`set`/`map` tidak bisa jadi nama variabel | Pesan error jelas ("kata cadangan"); daftar keyword tertulis di PRD |
| Literal kosong ambigu | Error yang menyarankan anotasi tipe |
| Const generics besar | Tahap 1 hanya N literal; fungsi yang menerima panjang berapa pun memakai `Span[T]` |
| Cek batas indeks dinamis | Dibuang optimizer bila terbukti aman; `scan` tidak butuh cek |

---

## 3. `if` dan `pick` sebagai expression 🟦

**Aturan**

- Boleh jadi nilai di posisi: initializer, argumen, operan, operan `return`, dan ekor blok.
- `if` sebagai nilai wajib punya `else`. `else if` ditulis langsung.
- Tipe semua cabang harus sama. Cabang yang berakhir `return` atau defect bertipe `Never` (tipe "tidak pernah kembali"), cocok dengan tipe apa pun.
- `pick` sebagai nilai wajib exhaustive (sudah dicek sema).

```cat proposal
fn abs(x: Int32) -> Int32 {
    if x < 0 { -x } else { x }
}

val label = if ok { "a" } else if warn { "b" } else { "c" }

val code = pick res {
    Ok(v)   { v }
    Fail(_) { return 1 }        // cabang bertipe Never
}
```

**Cara kerja compiler:** AST mendapat node `IfExpr` dan `PickExpr`. Sema menyatukan tipe cabang. LLVM: tiap cabang `br` ke blok gabungan yang memakai instruksi `phi` (memilih nilai sesuai cabang asal).

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Ambiguitas statement vs expression di akhir blok | Satu aturan: konstruksi terakhir di blok yang tipe harapannya bukan `Unit` adalah nilai; selain itu statement. Diuji kedua arah |
| `if` tanpa `else` dipakai sebagai nilai | Error khusus: "`if` sebagai nilai butuh `else`" |
| Bergantung pada unary minus | Dikerjakan bersama grup operator A (bagian 6) |

---

## 4. Aturan huruf dan grammar pattern `pick` ✅

**Aturan huruf**

| Kategori | Huruf | Contoh |
| --- | --- | --- |
| Nama tipe buatan pengguna: shape, choice, contract | Kapital di awal | `Point`, `Shape` |
| Nama builtin scalar dari daftar tertutup di bawah | Ejaan canonical; pencocokan ASCII case-insensitive | `Int32`, `int32`, `INT32` |
| Konstruktor tipe generic dan builtin non-scalar | Kapital di awal | `Array`, `Range`, `Vec`, `Result` |
| Varian `Result` dan `Maybe` | Kapital (pengecualian tertutup) | `Ok`, `Fail`, `Some`, `None` |
| Varian choice buatan pengguna | huruf kecil | `circle`, `rect`, `none` |
| Lainnya: variabel, fungsi, field, modul | huruf kecil | `count`, `read_file` |

Daftar scalar yang case-insensitive hanya `Bool`, `Char`, `Unit`, `Int8`/`Int16`/`Int32`/`Int64`/`Int128`, `UInt8`/`UInt16`/`UInt32`/`UInt64`/`UInt128`, `USize`, `Float32`/`Float64`/`Float128`, serta alias `Int`, `UInt`, dan `Float`. Pencocokan hanya melipat ASCII `A`–`Z`; ejaan internal tetap canonical, dengan alias `Int` → `Int64`, `UInt` → `UInt64`, dan `Float` → `Float64`. Ini tidak mengubah case sensitivity nama tipe buatan pengguna, parameter generic, constructor container, varian, field, fungsi, maupun identifier lain.

Compiler tetap memaksakan kapital di awal untuk deklarasi nama tipe dan huruf kecil di awal untuk kategori nama lain; pelanggaran adalah error dengan saran perbaikan. Pengecualian huruf kapital untuk varian tetap terbatas pada `Ok`, `Fail`, `Some`, dan `None`. Di luar daftar scalar builtin tertutup di atas, penulisan nama tetap case-sensitive.

**Grammar pattern**

```text
Pattern := _                     // buang nilai, tidak pernah mengikat nama
         | nama                  // binding (hanya di posisi non-choice)
         | tag                   // varian tanpa data
         | tag(Pattern, ...)     // varian dengan data
         | (Pattern, ...)        // tuple
```

**Membedakan tag dari binding (berdasarkan tipe)**

- Di posisi bertipe `choice`, sebuah nama **harus** varian dari tipe itu. Bukan varian = error: "`nome` bukan varian dari `Shape`; untuk tangkap-semua pakai `_`". Tidak ada fallback diam-diam menjadi binding.
- Di posisi bertipe lain (angka, string, tuple, dst.), nama huruf kecil adalah binding.
- `_` sebagai cabang sendiri berarti tangkap-semua: wajib terakhir dan membuat `pick` exhaustive. Cabang yang tak terjangkau memberi peringatan.

```cat proposal
choice Shape { circle(Float64), rect(Float64, Float64), none }

val area = pick s {
    circle(r)   { 3.14 * r * r }      // 'circle' varian, 'r' binding (Float64)
    rect(w, h)  { w * h }
    none        { 0.0 }
}

pick result {
    Ok(v)    { use(v) }
    Fail(_)  { return 1 }
}
```

Karena tidak ada fallback ke binding, menambah varian baru ke sebuah `choice` tidak pernah mengubah arti pola lama secara diam-diam.

**Cara kerja compiler:** parser Pattern rekursif; sema menentukan tipe tiap posisi lalu memutuskan tag atau binding. Exhaustiveness dihitung dengan algoritma matriks pola (memecah per varian sampai semua kasus tercakup). Pencarian varian case-insensitive di `choice_utils.cpp` dihapus. Tahap A: `tag`, `tag(a, b)`, `_`. Tahap B: tuple dan bersarang (setelah bagian 8).

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Fixture lama (`ok/FAIL`, `oK`) menjadi error | Diperbarui di PR yang sama; CI memastikan |
| Salah ketik nama varian | Error (bukan binding diam-diam) |
| Exhaustiveness bersarang rawan salah | Tes properti: bangkitkan tipe `choice` acak, bandingkan dengan enumerasi brute-force |

---

## 5. Bentuk `try` [parsial — menunggu acceptance]

**Aturan**

- `try` hanya sebagai initializer `val`: `val x = try <ekspresi bertipe Result>`.
- Shorthand (tanpa `else`): error type `E` harus **identik** dengan error type fungsi pemanggil.
- Form penuh `else err { ... }`: blok wajib berakhir bertipe `Never` (`return`, defect, `break`, `continue`).
- `var x = try` tetap ditolak. Tulis `val t = try f()` lalu `var x = t`.
- Satu `try` membuka tepat satu `Result`. Rangkaian `Result` ditulis sebagai beberapa `val ... = try`.

```cat proposal
fn load(p: Str) -> Result[Config, String] {
    val text = try read(p)                         // E = String, identik: shorthand sah
    val cfg  = try parse(text) else err {          // E berbeda: form penuh
        return Fail(err)
    }
    return Ok(cfg)
}
```

**Status implementasi (2026-10-11):** §5 tetap parsial sambil menunggu acceptance gate yang relevan. Repro awal menemukan bahwa `try` tidak mengonsumsi `Result` owned (reuse berujung UAF/double-free) serta shorthand salah membandingkan imported nominal generic type melalui alias. Ownership move dan payload transfer `Ok`/`Fail` kini diterapkan. Metadata nominal pada export catalog dan imported binding sama-sama memakai path source ternormalisasi; regresi menerima alias untuk tipe sama, menolak generic mismatch, dan membedakan dua module dengan basename sama. Clean build, suite penuh disposable, native ASan/LSan, dan fresh independent review atas snapshot terkini semuanya lulus/READY. Status tetap parsial sampai acceptance gate dicatat; jangan menandai §5 selesai lebih awal.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Nilai owned `Result` pada jalur sukses/gagal `try` | Implementasi dan tes source-reuse, OIR/drop, serta native ASan/LSan lulus; independent review READY, acceptance masih menunggu |
| Identitas error type nominal lewat import alias | Alias same-type diterima; generic mismatch dan tipe bernama sama dari file berbeda tetap ditolak; independent review READY, acceptance masih menunggu |
| Konversi error otomatis tidak ada | Tidak ada konversi diam-diam (sesuai Safe). Kontrak `Convert` dicatat di daftar tertunda |

---

## 6. Operator, presedensi, asosiativitas 🟦

**Tabel normatif** (tinggi ke rendah)

| Tingkat | Operator | Asosiativitas |
| --- | --- | --- |
| 1 | postfix: `.`, `()`, `[]`, `!` | kiri |
| 2 | unary: `-`, `not`, `~`, `ref` | kanan |
| 3 | `as` | kiri |
| 4 | `*` `/` `%` | kiri |
| 5 | `+` `-` | kiri |
| 6 | `<<` `>>` | kiri |
| 7 | `&` | kiri |
| 8 | `^` | kiri |
| 9 | `\|` | kiri |
| 10 | `<` `<=` `>` `>=` `==` `!=` `is` `in` | **non-asosiatif** |
| 11 | `and` | kiri |
| 12 | `or` | kiri |
| 13 | `..` `..=` | non-asosiatif |
| 14 | `=` `+=` `-=` `*=` `/=` `%=` dst. | kanan (statement) |

**Keputusan di dalam tabel**

- Bitwise berada **di atas** perbandingan (seperti Rust), supaya `a & b == c` tidak terbaca keliru seperti di C.
- Perbandingan non-asosiatif: `a < b < c` adalah error kompilasi.
- `and`/`or` short-circuit (operan kanan tidak dievaluasi jika hasil sudah pasti).
- `is` hanya untuk `ref`, membandingkan alamat.
- Compound assignment mengevaluasi sisi kiri **sekali**: `a[f()] += 1` memanggil `f` satu kali.

**Bertahap**

| Grup | Isi |
| --- | --- |
| A | `+ - * / %`, unary `-`, `not`, perbandingan, `and`, `or` |
| B | compound assignment |
| C | bitwise (`& \| ^ ~ << >>`), hanya tipe integer |
| D | `as`, `is`, `in`, range |

**Cara kerja compiler:** parser Pratt (parser operator yang mengambil prioritas dari tabel binding-power), dibuat langsung dari tabel di atas. LLVM, **mode debug**: `Int*` memakai `llvm.sadd.with.overflow` (dan padanannya) lalu cabang ke defect, sesuai §19. **Mode release**: `add` biasa yang membungkus (*wrapping*), tanpa cek dan tanpa cabang. `UInt*` selalu membungkus (§23).

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Pekerjaan besar | 4 grup; tiap grup punya tes tabel (input, pohon AST yang diharapkan) |
| Perilaku debug berbeda dari release pada overflow | Aturan tertulis di PRD: debug = defect (menangkap bug saat tes), release = wrap yang terdefinisi dan tidak merusak memori. Untuk kode sensitif ada `checked_*`, `wrapping_*`, `saturating_*`; lint untuk aritmetika `Int*` atas nilai dari input luar |

---

## 7. Range dan `scan item, i` 🟦

**Aturan**

- `a..b` eksklusif di ujung; `a..=b` inklusif. Keduanya bertipe `Range[T]`, `T` integer atau `Char`.
- Satu tipe dipakai untuk iterasi, `in`, dan slicing.
- `scan item, i over xs`: `i` bertipe `USize`, mulai 0, berlaku untuk iterable apa pun.

```cat proposal
scan n over 0..5 { println(n) }               // 0 1 2 3 4
scan n over 0..=255_UInt8 { ... }             // berhenti benar, tanpa overflow
scan item, i over names { println(f"{i}: {item}") }
if x in 10..20 { ... }
```

**Cara kerja compiler**

- Lexer: titik setelah digit dianggap bagian angka **hanya bila diikuti digit**. Jadi `0..10` dilex sebagai `0`, `..`, `10`. Token `..=` ditambahkan.
- Iterasi inklusif: periksa batas sebelum menaikkan penghitung, sehingga `0..=255_UInt8` selesai.
- LLVM: loop dengan `phi` untuk penghitung.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Konflik `0..10` dengan literal float | Aturan lexer di atas + tes: `1.5`, `1..5`, `1.`, `x.0.1` |
| Overflow pada range inklusif | Algoritma "periksa dulu, naikkan kemudian" + tes batas tipe |

---

## 8. Tuple 🟦

**Aturan**

- Tipe `(T, U)`, literal `(a, b)`, tuple satu elemen `(a,)`, `()` = `Unit`.
- Akses `t.0`, `t.1`.
- Destructuring `val (a, b) = t` dan pattern tuple di `pick`, memakai `_` dari bagian 4. Nested binding pattern seperti `val ((a, b), c) = t` untuk tuple plain, konkret, non-view/non-owned **[sudah]**. Nested destructuring atas field owned/view/generic, partial moves, dan assignment tetap belum didukung.
- Destructuring **menghabiskan** tuple (milik penuh). Elemen `_` dibebaskan di statement itu juga.

```cat proposal
fn min_max(xs: Span[Int32]) -> (Int32, Int32) { ... }
val (lo, hi) = min_max(data)
val pair = (10, "hello")
println(pair.1)
```

**Cara kerja compiler**

- Parser: `(` expr diikuti koma membentuk tuple; tanpa koma tetap grouping.
- Lexer: setelah `.`, terima integer sebagai proyeksi; `x.0.1` dipecah menjadi dua proyeksi, bukan float `0.1`.
- LLVM: struct anonim `{ i32, ptr }`.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Urutan pembebasan elemen `_` mengejutkan | Satu aturan tertulis + tes memori |
| Tuple tergoda dipakai menggantikan shape | Lint: tuple dengan lebih dari 3 elemen disarankan jadi shape |

---

## 9. Lifetime: klaim yang akurat dan dataflow ✅

**Rumusan PRD yang diusulkan** (menggantikan "full inference"):

> C@ tidak memakai anotasi lifetime bergaya `'a`. Compiler memeriksa pinjaman dengan **analisis aliran data (dataflow)**: sebuah pinjaman hidup sampai pemakaian terakhirnya di alur kontrol, bukan sampai akhir blok. Kasus umum disimpulkan otomatis, dan nama scope `[s]` hanya diminta ketika hubungannya ambigu.

**Cara kerja:** per fungsi, compiler membangun graf alur kontrol (CFG), menghitung *liveness* tiap `ref` (analisis mundur: apakah nilainya masih akan dipakai), lalu menandai pinjaman aktif dari pembuatannya sampai pemakaian terakhir. Konflik = pinjaman mutable yang tumpang tindih dengan pinjaman lain, atau pemilik dipindah/diubah selagi pinjaman masih hidup.

**Aturan elision (kasus umum tanpa `[s]`)**

```cat proposal
fn first(xs: ref Span[Int32]) -> ref Int32 { ... }       // satu parameter ref: hasil terikat ke xs

fn pick_longer[s](a: ref[s] Str, b: ref[s] Str) -> ref[s] Str { ... }   // dua parameter ref: wajib nama
```

- `ref mut[s]` **ditolak di v1** dengan pesan yang menjelaskan alasannya. Didaftarkan di daftar tertunda dengan syarat: bukti eksklusivitas peminjaman mutable.
- Ini tidak menghalangi self-hosting: parser dan state mutable cukup dipegang sebagai nilai milik, sedangkan `ref[s]` immutable sudah mendukung view atas sumber.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Dataflow lebih besar dari pemeriksaan per blok | Urutan pengiriman: versi per blok lebih dulu (lebih ketat, tidak pernah meloloskan kode tak aman), lalu dataflow melonggarkannya; melonggarkan tidak merusak kode lama |
| Klaim "Safe" dibaca sebagai bukti matematis | Rumusan diganti dengan **suite tes negatif**: use-after-move, dangling ref, aliasing mutable, baca sebelum init, indeks di luar batas. Kriteria Safe v1 = seluruh suite itu ditolak |
| Aturan elision tidak persis sama dengan sema | Cocokkan dengan `sema.cpp:1960–1979` sebelum PRD ditulis; selisih menjadi bug yang ditutup tes |

---

## 10. Inisialisasi default 🟦

**Aturan:** `var x: T` boleh tanpa nilai, tetapi dibaca sebelum diisi = error (sudah dilakukan compiler). Klaim "nol otomatis" dihapus dari §24.

**Status sintaks:** `[diterima compiler]` untuk aturan inisialisasi yang diuji pada irisan ini; ini bukan klaim bahwa gerbang v1 atau kualitas production-grade telah tercapai.

```cat
fn main() -> Int32 {
    var count: Int32            // sah, belum berisi
    count = 0                   // sekarang boleh dibaca
    return count
}
```

```cat error: Use of uninitialized value
fn main() -> Int32 {
    var count: Int32
    return count
}
```

Nilai default eksplisit lewat kontrak `Default` dengan `Tipe.default()` dicatat sebagai item tertunda.

**Risiko → penutup:** hampir nol. Analisis *definite initialization* (memastikan setiap jalur mengisi sebelum membaca) sudah ada dan teruji.

---

## 11. Literal numerik dan `Char` 🟦

**Aturan lexer**

| Bentuk | Aturan |
| --- | --- |
| Desimal | `1_000_000`: `_` di antara digit adalah pemisah |
| Radix | `0xFF`, `0b1010`, `0o755` |
| Suffix tipe | `32767_Int16`, `1.25_Float32`. Suffix numeric dikenali bila seluruh teks setelah `_` cocok secara ASCII case-insensitive dengan nama scalar numeric builtin (`Char`, tipe `Int*`/`UInt*`, `USize`, tipe `Float*`, atau alias `Int`/`UInt`/`Float`); tipe hasil tetap canonical. Selain itu `_` tetap pemisah, dan tail yang bukan digit/separator valid menghasilkan error literal |
| `Char` | Satu Unicode scalar value (U+0000–U+10FFFF tanpa surrogate), bertipe 32-bit; lexer men-decode UTF-8 |
| Escape | `\n \t \r \\ \' \" \0 \u{1F600}` |
| `Float128` | Literal diparse dengan `llvm::APFloat`, tipe LLVM `fp128` |

```cat proposal
val big   = 1_000_000
val mask  = 0xFF_UInt8        // suffix: "UInt8" adalah nama tipe
val n     = 0xAB_CD           // pemisah: "CD" bukan nama tipe
val heart = '\u{2764}'
```

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Bentrok `_` pemisah vs suffix, terutama di hex (`F`, `D` adalah digit hex) | Cocokkan seluruh suffix pada daftar tertutup secara ASCII case-insensitive; selain itu validasi digit/separator tetap menurut radix. Tes tabel: `0xFF_fLoAt32` (suffix), `0xFF_F` dan `0xAB_cD` (pemisah hex), `1_iNt8`, `1_0`, serta tail tak dikenal yang tetap error |
| Literal di luar rentang tipe | Error kompilasi: "32768 di luar rentang Int16" |

---

## 12. String, interpolasi, dan komentar dokumen 🟦

**Aturan**

- **Status implementasi:** Fondasi borrowed `Str`/owned `String` di bawah — [sudah]. F-string/interpolasi dan string builder tetap ditunda.

- `Str`/`str` adalah alias tipe **view UTF-8 borrowed** `{ptr, len}` (konsepnya setara Rust `&str`), bukan string owned. `String`/`string` adalah alias tipe **owned, immutable, move-only** `{ptr, len, cap}`; pada target 64-bit ukurannya 24 byte dengan alignment 8.
- Literal string bertipe `Str` dan menunjuk ke storage statis read-only. Literal tidak mengalokasikan heap dan tidak disalin; tidak ada konversi implisit dari `Str` ke `String`.
- `String.from(str)` membuat salinan byte UTF-8 eksplisit ke storage owned. `value.as_str()` menghasilkan view yang hidupnya terikat otomatis ke owner; `value.clone()` adalah salinan owned eksplisit.
- Move, parameter owned, dan return `String` mentransfer kepemilikan; setiap alokasi owned dibebaskan tepat sekali. `Str` tidak memiliki destructor dan tidak pernah membebaskan storage. OOM pada alokasi `String` menghentikan proses secara deterministik.
- Borrow checker menyimpulkan hubungan view-owner tanpa anotasi lifetime pada source; view yang dapat lolos dari owner lokal atau dipakai setelah owner dipindahkan/diubah ditolak saat kompilasi. View dari owner temporary boleh dipakai langsung sampai akhir full-expression, tetapi tidak boleh disimpan, dikembalikan, atau dilewatkan melalui join nilai `if`; simpan owner `String` pada binding terlebih dahulu. Field pada shape normal tidak boleh menyimpan `Str`; gunakan `String` untuk teks yang harus dimiliki.
- Sampai generic storage memiliki propagasi borrow dan element-drop glue, `Vec`/`Set`/`Map`/`Queue` menolak payload `Str` maupun `String`. Choice dan shape dengan payload/field owned `String` didukung bila recursive drop tersedia; `Maybe[ref Str]` adalah carrier terbatas untuk hasil opsional view.
- Jalur string statis tidak menambah alokasi, copy, atau runtime dispatch; hanya konversi owned eksplisit yang mengalokasikan. Error ownership harus menyertakan lokasi sumber, alasan jelas, hint tindakan, serta warna yang mengikuti kebijakan terminal; `NO_COLOR` dan kebijakan no-ANSI Windows tetap dihormati.
- Escape string sama dengan Char. Tidak ada raw string di v1.
- F-string/interpolasi dan string builder **ditunda**: lexer/parser, API builder, serta kontrak konversi teks belum menjadi bagian dari fondasi ini.
- `///` disimpan sebagai data dokumentasi dan dilekatkan ke deklarasi berikutnya. Di item non-`share` hasilnya peringatan. `claw doc` membuat dokumentasi dari item `share`.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Nested `{}` dan kutip di dalam interpolasi | Lexer bermode (mode string ↔ mode expression) + tes: `f"{f"{x}"}"`, `f"{{x}}"` |
| `///` tanpa deklarasi | Peringatan, bukan error |

---

## 13. Generic contract, `with`, dan lokasi `implements` 🟦

**Aturan**

- Pertahankan kata `with` (sesuai gaya bahasa dan PRD), tanpa menukar ke `:`.

```cat proposal
fn largest[T with Comparable](a: T, b: T) -> T { ... }
fn show_all[K with Hashable and Show, V](m: ref Map[K, V]) { ... }
implements Vector2 with Add[Vector2] { ... }
```

- Parameter tipe := `Nama (with Kontrak (and Kontrak)*)?`. Pembatas jelas karena `with` adalah kata kunci.
- **Aturan lokasi (orphan rule):**
  - `implements Shape { ... }` (metode) harus berada di modul shape.
  - `implements Shape with Kontrak { ... }` harus berada di modul shape **atau** modul kontrak.
  - Di modul ketiga = error. Dua `implements` untuk pasangan (Tipe, Kontrak) yang sama di seluruh program = error.

**Cara kerja compiler:** `parseTypeParameterList` menerima `with`; `parseImplementsDeclaration` menerima tipe lengkap (termasuk argumen generic) setelah `with`. Sema memeriksa aturan orphan memakai graf modul dari keputusan modularitas.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Aturan orphan terlalu ketat | Memang tujuannya: mencegah konflik implementasi di pustaka berbeda. Dua kasus (modul shape, modul kontrak) mencakup hampir seluruh kebutuhan nyata |
| Substitusi tipe generik kompleks | Dibangun bertahap di v0.5; mulai dari satu kontrak per parameter, lalu `and` |

---

## 14. `slice` 🟦

**Aturan**

- Satu bentuk publik: indexing dengan range.

```cat proposal
val part  = text[2..5]            // Str view (pinjaman dari text)
val chunk = data[1..=3]           // Span[Int32]
val safe  = text.get(2..5)        // Maybe[ref Str], tidak defect
```

- Ujung kiri `a`, kanan `b`, wajib `a <= b <= panjang`; melanggar = defect, kecuali lewat `get` yang mengembalikan `Maybe`.
- Unit indeks `Str` = byte, dan kedua ujung harus jatuh di batas karakter UTF-8; kalau tidak, defect.
- Hasil selalu **borrowed `Str` view** yang terikat otomatis ke owner sumbernya. Ingin salinan milik: `String.from(part)`; tidak ada konversi `Str.from` atau `.to_owned()`.
- Method `slice(start, length)` dihapus dari API publik; ia hanya menjadi target lowering internal.

**Cara kerja compiler:** LLVM menghitung `ptr = base + a` dan `len = b - a` setelah cek batas (GEP + `sub`), lalu membentuk struct view. Ini juga menyelesaikan lowering `Span.slice` yang belum ada menurut `builtin-methods.md`.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Defect pada slicing berbahaya untuk input pengguna | `get(...)` mengembalikan `Maybe`, jadi jalur aman tersedia |
| Batas karakter UTF-8 | Cek `(byte & 0xC0) != 0x80` pada kedua ujung; tes dengan string multibyte |

---

## 15. `foreign c`, annotation, dan `rewrite` 🟦

**`foreign c`:** jadi bentuk resmi deklarasi fungsi C. Hasil parse diisi ke struktur *external contract* yang sama dengan yang dipakai `claw.toml`, jadi tidak ada dua jalur logika.

```cat proposal
foreign c {
    fn strlen(s: raw CStr) -> USize
}

fn length(s: ref Str) -> USize {
    raw { return strlen(s.as_c_str()) }
}
```

- Panggilan `foreign` hanya sah di dalam `raw { }` (sesuai §10) dan harus dibungkus fungsi safe.
- `claw.toml` hanya menyimpan opsi link (pustaka, path), bukan signature. Signature ganda di toml dan source = error.

**Annotation:** token `@` ditambahkan ke lexer. Untuk v1, **hanya annotation bawaan compiler**: `@constructor`, `@test`, `@deprecated`.

```cat proposal
@constructor                // compiler menulis new(x, y) di modul ini
shape Point {
    share x: Int32
    share y: Int32
}
```

`@constructor` dibangkitkan di modul yang sama, sehingga aturan "literal hanya di modul pendefinisi" tidak dilonggarkan. Ini memenuhi syarat v1 untuk menghapus boilerplate `new`.

**`rewrite` (macro dengan postfix `!`):** perlu fase ekspansi, kebersihan nama (*hygiene*), dan introspeksi AST. Pekerjaannya sebesar fitur tersendiri. ✅ Diputuskan: masuk **v1.x**, bukan v1.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| `foreign c` membuka celah Safe | Aturan "hanya di `raw` + pembungkus safe" + tes negatif untuk pemanggilan di luar `raw` |
| Annotation bawaan berkembang liar | Daftar annotation bawaan tertutup di PRD; menambah satu butuh perubahan PRD |

---

## 16. Sinkronisasi dua PRD 🟦

- **Satu PRD otoritatif** (yang panjang, bagian 1–38). Dokumen ringkas tidak ditulis tangan lagi, tetapi dibuat otomatis dari baris `Ringkas:` yang ditulis di tiap bagian PRD panjang.
- Normalisasi encoding ke UTF-8 pada dokumen ringkas (kerusakan karakter yang disebut proposal).
- Pilih satu contoh `main` kanonis: `fn main() -> Int32` dengan `return 0`; bentuk `fn main() {}` ditandai "varian sah".
- Arena: PRD menandai \[sudah\], tetapi proposal tidak mengauditnya. Masukkan ke audit berikutnya (dan perbaiki contoh yang memakai `Node(data: ..., next: ...)`).

**Risiko → penutup:** ringkasan otomatis tidak bisa tertinggal, karena dibuat dari sumber yang sama.

---

## 17. Modularitas ✅

- Satu file = satu modul; folder = namespace; tidak ada `modules.cat` dan tidak ada kata kunci `pub`.
- `share` adalah satu-satunya aturan visibilitas. Tanpa `share`, item hanya terlihat di file itu.
- `import src.math` membawa namespace (`math.add(1, 2)`); `import src.math.{ add }` membawa item langsung; dua modul berakhiran sama memerlukan alias.
- `import super.{ ... }` dipakai untuk file di folder yang sama (hanya satu level); folder lain selalu `import src....`. Aturannya deterministik, jadi bukan dua cara untuk satu hal. ✅ Dipertahankan agar penulisan impor pendek.
- Siklus impor = error. Tipe privat tidak boleh bocor lewat signature `share`.
- LLVM: simbol `path.modul.nama`; `share` → `external`, privat → `internal`; gabungkan semua modul LLVM sebelum optimasi untuk v1.

---

## 18. Rencana milestone

| Versi | Isi | Bukti selesai |
| --- | --- | --- |
| v0.1 | Konsistensi dasar: doc-test, label status, modularitas, aturan shape, default init, casing varian, satu PRD otoritatif | Semua fence diklasifikasikan; `cat` lulus, `cat error:` ditolak, `cat proposal`/`text` dilaporkan terlewati |
| v0.2 | Ekspresi: operator grup A–B, `if`/`pick` expression, tipe `Never`, aturan `try` | Tes tabel presedensi + tes cabang |
| v0.3 | Data: tuple, range, `scan item, i`, literal `[...]` + `Array[T, N]` (N literal), pattern penuh, slicing | Tes properti exhaustiveness |
| v0.4 | Literal dan teks: radix, `Char`, escape, f-string, `///` | Tes tabel lexer |
| v0.5 | Tipe lanjutan: operator grup C–D, generic `with`, aturan orphan, kontrak operator overloading, `@constructor` | Boilerplate `new` hilang |
| v0.6 | FFI: `foreign c`, annotation bawaan | Tes negatif FFI |
| v0.7 | Borrow checker berbasis dataflow (CFG + liveness) menggantikan versi per blok | Suite tes negatif tetap ditolak; program valid yang dulu ditolak kini lolos |
| v0.8 | Optimasi release: overflow wrap tanpa cek, `noalias` untuk `ref mut`, cek batas dibuang bila terbukti aman | Benchmark inti; IR dibandingkan dengan C setara |
| v0.9 | Pengerasan: suite tes negatif Safe, fuzzing parser, benchmark Fast, audit Arena | Semua kriteria di bawah terpenuhi |
| **v1.0** | Gerbang rilis | Daftar periksa di bawah |
| v1.x | `rewrite` (macro); **self-hosting dimulai** (compiler ditulis dengan C@) | Compiler C@ membangun dirinya sendiri |

**Daftar periksa gerbang v1** (usulan, kamu yang menetapkan):

- **Safe:** seluruh suite tes negatif ditolak; tidak ada use-after-free, dangling ref, atau data race tanpa `raw`/`foreign`.
- **Simple:** tidak ada dua cara untuk satu hal; tidak ada boilerplate yang bisa dibangkitkan compiler; setiap contoh PRD lulus doc-test.
- **Fast:** selisih waktu ≤ 10% dari C setara (clang -O3) pada benchmark inti; release tanpa cek overflow, `noalias` pada `ref mut`, cek batas indeks dibuang bila terbukti aman; tidak ada biaya runtime tersembunyi.
- Dua prasyarat self-hosting yang tidak dibahas proposal: pustaka standar untuk file I/O, string builder, `Vec`, dan `Map` harus cukup.

---

## 19. Keputusan dari celah di luar proposal

1. **`import super`** ✅ dipertahankan (bagian 17). Catatan: di bahasa lain `super` berarti folder induk; kalau nanti membingungkan pengguna, nama bisa diganti tanpa mengubah aturan.
2. **Overflow di release** ✅ membungkus tanpa cek; debug = defect; `checked_*`/`wrapping_*`/`saturating_*` eksplisit. Cek batas indeks tetap ada (kalau dibuang, klaim Safe gugur). `ref mut` eksklusif, sehingga pointer-nya ditandai `noalias` di LLVM.
3. **Arena** sudah ditandai \[sudah\] di PRD tetapi di luar cakupan audit proposal; contohnya masih memakai sintaks named-argument yang dihapus. Masuk audit v0.9.

---

## 20. Keputusan penutup ✅ (diputuskan agar tidak ada yang menggantung)

1. **`Maybe`**: sudah dipakai PRD sendiri (`Maybe[ref Node]`, `Some`, `None`), jadi final: `Maybe[T]` dengan varian `Some(T)` dan `None`.
2. **`as` untuk penyempitan**: integer → integer yang tidak muat = defect di debug dan terpotong (*truncate*) di release; float → integer memakai saturasi (di luar rentang menjadi nilai batas, NaN menjadi 0; LLVM `llvm.fptosi.sat`); `UInt32` → `Char` hanya lewat `Char.from(n)` yang mengembalikan `Maybe`. Konversi yang aman untuk nilai dari luar: `x.to[Int8]()` mengembalikan `Maybe`.
3. **Nama kontrak**: konversi teks untuk f-string = `Show`; nilai awal = `Default`; thread = `Send` dan `Sync`.
4. **Batas Fast untuk gerbang v1**: selisih waktu ≤ 10% dari C setara (dikompilasi dengan clang -O3) pada benchmark inti.
5. **Closure**: capture default meminjam (`ref`), `move` untuk mengambil kepemilikan; `return` di dalam closure keluar dari closure.
6. **Penulisan konstanta `static`**: huruf kecil, mengikuti aturan huruf umum.
