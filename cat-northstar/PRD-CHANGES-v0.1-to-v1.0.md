# Perubahan PRD C@ — v0.1 sampai v1.0

Dokumen ini adalah daftar kerja: bagian PRD mana yang diubah, apa isinya, kapan, dan bagaimana membuktikannya selesai. Dasar keputusan: *Paket Solusi C@ (final)*. Hal-hal setelah v1 ada di dokumen terpisah: *Roadmap C@ setelah v1*.

---

## 1. Doc-test: bukti nyata bahwa PRD cocok dengan compiler

**Cakupan PRD utama saat ini** (`cat-northstar/PRD.md`):

| Kategori fence | Jumlah | Perilaku |
| --- | ---: | --- |
| `cat` | 3 | Wajib lolos `claw check` |
| `cat error: <potongan pesan>` | 1 | Wajib ditolak dan keluaran compiler wajib memuat potongan diagnostik |
| `cat proposal` | 13 | Sintaks belum didukung compiler saat ini; dilewati dan dilaporkan dengan nomor baris |
| `text` | 2 | Diagnostik target dan grammar ilustratif, bukan program C@; dilewati dan dilaporkan |
| Tanpa label / label tak dikenal | 0 | Menggagalkan runner sampai klasifikasinya diperbaiki |

Tag fence menyatakan **cakupan test terhadap compiler**, bukan apakah keputusan desain telah diratifikasi. Karena itu `cat proposal` tidak menurunkan status keputusan desain, tetapi juga tidak menjadi bukti implementasi.

Runner berada di `claw/tests/docs/run_doc_tests.py`, dipanggil melalui `claw/tests/docs/run_doc_tests.sh` dan suite penuh. Ia mengekstrak fence Markdown, menjalankan blok `cat`/`cat error:` dengan `claw check`, memeriksa kode keluar serta potongan diagnostik, dan mencetak jumlah serta baris semua blok yang dilewati. Fence tanpa label atau dengan label yang tak dikenal adalah kegagalan, bukan pengecualian diam-diam.

**Validasi irisan ini:** empat blok executable diperiksa (tiga lolos dan satu ditolak dengan diagnostik yang diharapkan); 13 contoh sintaks proposal dan dua blok non-program tercatat terlewati. Ini bukan bukti bahwa seluruh fitur v0.1 atau PRD sudah terimplementasi.

---

## 2. Perubahan global (berlaku di seluruh PRD)

**G2:** [FEATURE-STATUS.md](FEATURE-STATUS.md) adalah rumah kanonik status per irisan yang dapat diuji mandiri. Keputusan semantik, lingkup implementasi, bukti penerimaan, sisa, dan dimensi non-compiler memiliki kolom terpisah. Satu label kanonik tidak menggantikan kolom tersebut. G2 belum ditutup hanya karena ledger/validator tersedia; perlu audit cakupan, review independen, dan CI Linux serta Windows UCRT64 pada head PR yang sama.

| # | Perubahan | Versi |
| --- | --- | --- |
| G1 | Tag semua blok kode (`cat`, `cat error:`, `cat proposal`, `text`) | v0.1 |
| G2 | Tiap fitur diberi satu label status: `[diterima compiler]`, `[diusulkan]`, `[diputuskan]` | v0.1 |
| G3 | Tulis ulang semua `Tipe(field: ...)` menjadi `Tipe.new(...)` (di dalam modul pendefinisi boleh literal `Tipe { ... }`) | v0.1 |
| G4 | Terapkan aturan huruf: kapital hanya nama tipe + `Ok/Fail/Some/None`; varian buatan pengguna huruf kecil | v0.1 |
| G5 | `Maybe[T]` dengan varian `Some`/`None` sudah dipakai PRD (contoh `Maybe[ref Node]`); pastikan tidak ada sisa nama `Option` | v0.1 |
| G6 | Satu contoh `main` kanonis: `fn main() -> Int32` dengan `return 0`; `fn main() {}` ditandai varian sah | v0.1 |
| G7 | Normalisasi encoding dokumen ringkas ke UTF-8; dokumen ringkas dibuat otomatis dari baris `Ringkas:` | v0.1 |
| G8 | Tidak ada `modules.cat`, `pub`, atau `realm` di contoh mana pun | v0.1 |

---

## 3. Perubahan per bagian PRD

Kolom **Versi** dibagi dua: *D* = target perubahan teks PRD, *K* = target implementasi compiler, bukan bukti bahwa pekerjaan selesai. Nomor § pada tabel ini berasal dari struktur PRD terdahulu; pemetaan heading PRD aktif dan bukti atomiknya berada di [ledger G2](FEATURE-STATUS.md). Simbol keputusan historis dan metadata parsial tidak menjadi label G2 kedua.

| § | Topik | Perubahan | Versi | Tes penerimaan |
| --- | --- | --- | --- | --- |
| 3 | Tipe bawaan | `Char` = Unicode scalar; `Float128` literal; nama `Maybe` | D v0.1, K v0.4 | blok `cat` lolos |
| 4 | Literal dan tipe | Literal baru `[]`, `vec[]`, `set()`, `map{}`; hapus `Vec[1,2,3]`, `Set[...]`, `Map{...}` lama; `Array[T, N]`; tuple lengkap; aturan huruf | D v0.1, K v0.3 | literal kosong tanpa tipe → error |
| 5 | `try` | Hanya sebagai initializer `val`; error type identik; blok `else` bertipe `Never` | D v0.1, K v0.2 | `var x = try` → error; `return try f()` → error |
| 6 | Modul dan import | Hapus `modules.cat`/`pub`; satu file = satu modul; namespace import; `super` untuk folder yang sama; siklus = error; tipe privat tidak bocor lewat `share` | D v0.1, K v0.1 | tes modul: siklus, bocor, nama ganda |
| 7 | Kondisional | `if`/`pick` sebagai nilai; `else if` langsung | D v0.1, K v0.2 | `val x = if c { 1 } else { 2 }` lolos; tanpa `else` → error |
| 8 | Shape/choice | Contoh `Config` ditulis ulang; varian lowercase; `new` di `implements` | D v0.1 | contoh lolos |
| 9 | Lifetime/borrow | Rumusan dataflow; elision; `ref[s]` saat ambigu; `ref mut[s]` ditolak | D v0.1, K v0.7 | suite tes negatif Safe |
| 10 | `foreign c` / `raw` | `foreign c` jadi bentuk resmi; `claw.toml` hanya opsi link; panggilan hanya di `raw` | D v0.1, K v0.6 | panggil di luar `raw` → error |
| 11 | `Anchor`, `scope`, Arena | Contoh `Node(...)` dan `Cache(stored: ...)` ditulis ulang ke `.new(...)`; Arena diberi label `[eksperimental]` dan dikeluarkan dari gerbang v1; contoh siklus (doubly linked, graph) diganti `vec` + indeks | D v0.1, K v0.9 (audit) | contoh lolos; audit tertulis |
| 12 | `Str` | Tambah escape `\n \t \\ \" \u{..}` | D v0.1, K v0.4 | string berisi `"` lolos |
| 13 | Entry point | Satu contoh kanonis (G6) | D v0.1 | contoh lolos |
| 14 | Contract / implements | `T with Kontrak`; aturan orphan; contoh `Buffer`, `Circle` ditulis ulang | D v0.1, K v0.5 | `implements` di modul ketiga → error |
| 15 | Operator overloading | Tabel operator dan presedensi; contoh `Vector2` ditulis ulang | D v0.1, K v0.2 dan v0.5 | tes tabel presedensi |
| 16 | Interpolasi | Grammar f-string, `{{ }}`, multiline | D v0.1, K v0.4 | `f"{{x}}"` → teks literal |
| 18 | Cast `as` | Tambah aturan penyempitan | D v0.1, K v0.5 | tes tabel konversi |
| 19 | Defect | Bedakan debug dan release untuk overflow | D v0.1, K v0.8 | tes debug menangkap, tes release membungkus |
| 20 | Komentar | `///` jadi data dokumentasi; peringatan di item non-`share` | D v0.1, K v0.4 | `claw doc` menghasilkan keluaran |
| 21 | Literal numerik | `_` pemisah, radix, suffix (aturan "nama tipe persis") | D v0.1, K v0.4 | `0xAB_CD`, `0xFF_UInt8`, `1_000` |
| 22 | `is` / identitas | Hanya untuk `ref`; contoh `Config` ditulis ulang | D v0.1, K v0.5 | `1 is 1` → error |
| 23 | Overflow `UInt` | Tambah `checked_*`, `wrapping_*`, `saturating_*` | D v0.1, K v0.8 | tes tabel |
| 24 | Inisialisasi | Hapus klaim nol otomatis; contoh `var x: Int32 = 0` | D v0.1 | baca sebelum isi → error |
| 25 | Destructuring | Grammar pattern penuh; `_` | D v0.1, K v0.3 | tes tuple bersarang |
| 26 | Range | `Range[T]`; `..` eksklusif, `..=` inklusif; `in` | D v0.1, K v0.3 | `0..=255_UInt8` selesai |
| 27 | `scan item, i` | `i: USize` dari 0 | D v0.1, K v0.3 | iterasi range dan array |
| 28 | Chaining | Satu `try` = satu `Result` | D v0.1 | contoh lolos |
| 31 | Built-in | Hapus `slice(...)` publik; indexing range + `get`; hapus `.new()/.from()` koleksi | D v0.1, K v0.3 | `text[2..5]`, `get` lolos |
| 33 | Implicit return | Hanya ekspresi terakhir blok | D v0.1, K v0.2 | ekspresi di tengah blok → peringatan |
| 34 | `if`/`pick` ekspresi | Aturan tipe cabang, `Never` | D v0.1, K v0.2 | cabang tipe berbeda → error |
| 35 | Bitwise | Presedensi di atas perbandingan; hanya integer | D v0.1, K v0.5 | `a & b == c` terbaca `(a & b) == c` |
| 36 | Compound assign | Sisi kiri dievaluasi sekali | D v0.1, K v0.2 | `a[f()] += 1` memanggil `f` sekali |
| 37 | `rewrite` | Label `[diusulkan]`, dipindah ke dokumen roadmap v1+ | D v0.1 | tidak ada contoh `cat` di bagian ini |
| 38 | Annotation | Hanya bawaan: `@constructor`, `@test`, `@deprecated`; sisanya roadmap | D v0.1, K v0.5 | `@constructor` membuat `new` |
| baru | Closure | Bagian baru di PRD: bentuk pendek dan bentuk blok; capture default meminjam, `move` untuk mengambil kepemilikan; tipe parameter `fn(A) -> R` | D v0.1, K v0.5 | closure yang mengubah variabel terpinjam → error |

Bagian yang tidak muncul di tabel (§1, §2, §17, §29, §30, §32) tidak ikut diaudit proposal. Tugas v0.1: periksa aturan huruf dan contoh lama di sana.

---

### Keputusan tambahan: pembagi integer nol (18 Oktober 2026)

PRD §6 menetapkan defect runtime untuk builtin integer `/`, `%`, `/=`, dan `%=` dengan pembagi nol, termasuk konstanta pada konteks yang sah, seragam pada debug/release. Float tetap IEEE-754; aturan overflow signed dan batas versi tidak berubah. Penerimaan memerlukan matriks konstanta/dinamis, signed/unsigned, short-circuit, compound/evaluasi sekali, diagnostik dan exit nonzero lintas platform, serta IR yang tidak menjalankan operasi tidak valid. PR #23 masih terbuka dan tidak dikreditkan ke baseline main. Keputusan ini tidak menyelesaikan milestone v0.8 atau menyediakan profil target debug.

## 4. Milestone final v0.1 sampai v1.0

| Versi | Isi | Bukti selesai |
| --- | --- | --- |
| v0.1 | Seluruh perubahan teks PRD (kolom D), doc-test terpasang, modularitas (K), aturan `new` | Semua blok `cat` lulus; blok `error:` ditolak |
| v0.2 | Operator grup A–B, `if`/`pick` ekspresi, `Never`, aturan `try`, implicit return | Tes tabel presedensi, tes cabang |
| v0.3 | Tuple, range, `scan item, i`, literal koleksi, `Array[T, N]`, pattern penuh, slicing | Tes properti exhaustiveness |
| v0.4 | Radix, `_`, suffix, `Char`, escape, f-string, `///` | Tes tabel lexer |
| v0.5 | Operator grup C–D, generic `with`, aturan orphan, kontrak operator, `@constructor`, closure | Boilerplate `new` hilang; `map`/`filter` dengan closure berjalan |
| v0.6 | `foreign c`, annotation bawaan | Tes negatif FFI |
| v0.7 | **Borrow checker dataflow** (CFG, liveness) menggantikan versi per blok | Suite tes negatif Safe tetap ditolak; program valid yang dulu ditolak kini lolos |
| v0.8 | **Optimasi release**: overflow wrap tanpa cek, `noalias` untuk `ref mut`, cek batas dibuang bila terbukti aman | Benchmark inti; IR dibandingkan dengan C setara |
| v0.9 | Pengerasan: fuzzing parser, audit Arena, uji ketahanan | Tidak ada crash dari input acak |
| **v1.0** | Gerbang rilis | Daftar periksa di bawah |

---

## 5. Gerbang v1.0

- **Safe:** seluruh suite tes negatif ditolak (use-after-move, dangling ref, aliasing mutable, baca sebelum init, indeks di luar batas, kebocoran tipe privat). Tidak ada use-after-free atau data race tanpa `raw`/`foreign`.
- **Simple:** tidak ada dua cara untuk satu hal; tidak ada boilerplate yang bisa dibangkitkan compiler; setiap contoh PRD lulus doc-test.
- **Fast:** selisih waktu ≤ 10% dari C setara (clang -O3) pada benchmark inti; release tanpa cek overflow; `noalias` aktif; tidak ada biaya runtime tersembunyi.
- **Stabilitas:** setelah v1, perubahan yang merusak kode lama hanya lewat *edition* (lihat dokumen roadmap).
- **Prasyarat self-hosting:** pustaka standar cukup (file I/O, string builder, `vec`, `map`).
