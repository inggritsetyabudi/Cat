# `Range[Int8]` dan scan signed (2026-10-13)

Slice terbatas PRD §7 ini menambahkan konstruksi/inferensi `Range[Int8]`, membership `Int8 in Range[Int8]`, scan biasa dan scan berindeks `USize`. §7 tetap parsial; dokumen PRD/milestone tidak ditandai selesai. `Str` slicing tetap hanya `Range[Int32]`. Kontrak, source, runtime, dan fixture Arena tidak disentuh.

## Baseline sebelum perubahan

Snapshot `HEAD` sebelum edit dibangun terpisah dengan CMake/Ninja (`Debug`). Repro Cat nyata mencakup anotasi/inferensi `Range[Int8]`, membership, `scan item, index`, serta batas `-128_Int8` dan `127_Int8`. Compiler menolak program dengan diagnostik aktual:

- `Only Range[Int32], Range[UInt8], and Range[Char] are supported; other integer widths are not implemented yet.`
- `Range bounds currently support only Int32, UInt8, or Char with matching types, got Int8 and Int8.`
- `An indexed scan is currently supported only over Range[Int32], Range[UInt8], or Range[Char].`
- Membership `-1_Int8 in Range[Int32]` juga ditolak: ``Operator `in` requires a matching Char/Range[Char], Int32/Range[Int32], or UInt8/Range[UInt8] pair, got Int8 and Range[Int32].``

Jadi gap-nya bukan pada literal `Int8`, melainkan allowlist range/membership/scan yang belum memasukkan width tersebut.

## Implementasi

- Sema menerima hanya `Int8` sebagai width baru di samping `Int32`, `UInt8`, dan `Char`. Bound dan operand membership tetap harus bertipe sama.
- `Range[Int8]` memakai agregat LLVM `{ i8, i8, i1 }` dan ABI layout tiga byte; layout `Range[UInt8]` dan `Range[Int32]` tidak diubah.
- Membership dan scan `Int8` menggunakan perbandingan signed (`sge`, `slt`, `sle`), bukan unsigned.
- Scan tanpa indeks maupun dengan indeks `USize` menggunakan iterasi narrow `i8`; scan inklusif mengenali item terminal sebelum increment. `-128_Int8..=127_Int8` menghasilkan tepat 256 item, berakhir di indeks 255, dan tidak wrap.
- Endpoint dievaluasi sekali. Range equal-exclusive/reversed kosong; equal-inclusive memuat satu nilai.
- `Str` slicing tetap ditolak untuk `Range[Int8]`, `Range[UInt8]`, dan `Range[Char]`; hanya `Range[Int32]` yang tetap diterima.
- Agar nilai hasil scan dapat diuji sebagai signed, perubahan ABI printer `Int8` dicatat terpisah di [`2026-10-13-int8-runtime-print-abi.md`](./2026-10-13-int8-runtime-print-abi.md). Runtime C tidak diubah.

## Regresi

- Native Cat fixture memeriksa anotasi/inferensi, membership exclusive/inclusive dan signed edges, scan biasa/berindeks, index awal 0, item `-128`/`127`, full range 256 nilai, empty/equal/reversed, dan endpoint side-effect tepat sekali.
- Frontend/OIR menolak pasangan bound `Int8`/`UInt8` dan `Int8`/`Int32`, membership silang antara `Int8`/`UInt8`/`Int32`, width lain, serta slicing `Str` dengan `Range[Int8]`.
- Golden OIR dan backend LLVM mengunci tipe item, index `USize`, layout aggregate, signed compare, narrow increment, dan terminal equality.
- Tes existing `Range[UInt8]` dan `Range[Int32]` tetap dijalankan sebagai regresi.

## Verifikasi

- Baseline snapshot sebelum edit berhasil dibangun; repro Cat gagal dengan diagnostik di atas.
- Setelah perubahan, `claw check`, `air`, `oir`, `llvm`, `build`, dan eksekusi native pada fixture Cat Int8 lulus.
- Focused frontend/OIR/backend/native suites dan full suite (`bash claw/tests/run_all_tests.sh`) lulus pada disposable copy; `FULL_SUITE_EXIT=0`. Artifact runner berada di copy, bukan worktree utama.
- Fixture printer dan range lulus ASan/UBSan/LSan (`detect_leaks=1`, `halt_on_error=1`); stdout sanitized identik dengan run native biasa.
