# Range `Int32` dan membership (2026-10-12)

Implementasi parsial PRD §7. Slice ini mendukung nilai `Range[Int32]` dan `Int32 in Range[Int32]`; §7 tetap parsial. Saat slice ini dibuat, iterasi `scan` belum tersedia; tindak lanjutnya kini dicatat terpisah di [2026-10-12-int32-range-scan.md](2026-10-12-int32-range-scan.md). Slicing, `Range[Char]`, dan lebar integer lain tetap belum diimplementasikan. Arena tidak disentuh.

## Baseline

Sebelum perubahan, lexer belum memiliki token `..`, `..=`, atau keyword `in`, sehingga bentuk range tidak dapat dipakai sebagai nilai atau operator membership. Aturan numerik yang sudah ada membedakan desimal hanya jika titik diikuti digit; `1.5` tetap float, `1.` tetap single-dot/projection, dan `.0.1` tetap rangkaian projection tuple.

## Implementasi

- Lexer mengenali `..`/`..=` dengan longest match dan `in` sebagai operator. Titik desimal tetap dikonsumsi oleh angka hanya saat karakter setelah titik adalah digit.
- Parser mengikuti tabel presedensi PRD: `in` satu tingkat dengan perbandingan non-asosiatif; range lebih rendah dari `or` dan juga non-asosiatif. Untuk bentuk contoh PRD `x in a..b`, parser membentuk membership dengan nilai range sebagai operand kanan.
- Sema menerima literal batas konkret/plain `Int32` dan `Range[Int32]`; operator `in` mensyaratkan operand kiri `Int32` dan kanan `Range[Int32]`. Perbandingan antar-Range ditolak.
- `Range[Int32]` adalah nilai plain berisi start, end, dan flag inclusive. OIR memakai `range_exclusive`, `range_inclusive`, dan `in`; LLVM memakai aggregate `{ i32, i32, i1 }` dengan parameter/return ABI langsung.
- Membership menghitung `value >= start` dan `value < end` atau `value <= end` sesuai flag. Rentang kosong/terbalik tidak memiliki anggota secara alami dari pemeriksaan batas; inclusive dengan batas sama memiliki satu nilai. Kedua batas dievaluasi sekali dari kiri ke kanan.

## Regresi

- Fixture native `claw/tests/native/fixtures/revise_int32_range.cat` menguji exclusive/inclusive, membership pada batas, empty/reversed/equal ranges, batas negatif, rentang sebagai parameter dan return fungsi, evaluasi endpoint ber-efek samping sekali, literal `1.5`, proyeksi `nested.0.1`, aritmetika yang menjadi batas (`1..2 + 3`), membership dengan `and`/`or`, dan range yang diberi kurung.
- Golden `claw/tests/oir/golden/int32-range.oir` mengunci operasi OIR dan ABI Range.
- Harness backend memeriksa struct ABI, extraction, signed comparisons, dan pemilihan batas inclusive/exclusive.
- Fixture frontend/OIR negatif mencakup endpoint `Char`, `Int64`, dan float, `Range[Char]`, operand membership salah, chaining range, perbandingan Range, serta `1.` agar tidak tertukar dengan range.
- Harness native memastikan endpoint `Char` ditolak sebelum binary native dibuat.

## Verifikasi

Smoke check/AIR/OIR/LLVM dan native build lulus. Tes presedensi campuran dan signed Int32 extrema juga lulus; overflow endpoint ditolak dengan diagnostik Int32. Output native menunjukkan setiap endpoint ber-efek samping hanya sekali dan hasil batas sesuai. Clean configure/build dan `bash claw/tests/run_all_tests.sh` setelah cakupan presedensi diperluas lulus di disposable copy (`FULL_SUITE_RESULT=PASS`).
