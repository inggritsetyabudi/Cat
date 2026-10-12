# Range-view `Str` dengan `Range[Int32]` (2026-10-12)

Slice implementasi terarah untuk kontrak PRD §14 dan operator range PRD §7. Perubahan dibatasi pada `Str` + `Range[Int32]`; PRD tidak diubah dan status §14 tetap tidak diklaim selesai. Arena dan workstream Char tidak disentuh.

## Kondisi awal

- `text[0..2]` ditolak karena indexing hanya menerima `USize`; `text.get(0..2)` ditolak karena `Str.get` belum tersedia.
- `.slice(start, length)` diterima dan membentuk view, tetapi baseline `AéB.slice(1, 1)` menghasilkan byte pertama `é` (`0xC3`) saja, sehingga view dapat membelah karakter UTF-8.
- Workstream berjalan pada branch terpisah dari PR #6 untuk Char.

## Implementasi

- `Str[Range[Int32]]` kini menghasilkan borrowed `ref Str`; endpoint adalah offset byte, dengan `..` eksklusif dan `..=` inklusif. Untuk validasi UTF-8, ujung efektif half-open adalah `b` pada `..` dan `b + 1` pada `..=`; offset raw inklusif `b` boleh merupakan continuation byte jika hasilnya tetap berakhir pada batas scalar.
- `Str.get(Range[Int32])` menghasilkan `Maybe[ref Str]`. Bounds/range/UTF-8 boundary yang tidak valid menjadi `None`, bukan defect.
- Direct indexing defects untuk endpoint negatif, reversed, out-of-range, atau di tengah sequence UTF-8.
- LLVM memeriksa tanda, urutan, bounds, dan byte boundary sebelum membentuk pointer view. Endpoint inclusive dilebarkan ke `i64` sebelum `+1`, sehingga `INT32_MAX` tidak wrap; effective exclusive endpoint tetap harus memenuhi panjang byte sebelum pointer view dibentuk.
- Ownership mempertahankan provenance receiver untuk direct view dan hanya mengakui `Maybe[ref Str]` sebagai borrowed carrier khusus; tidak ada perubahan umum untuk borrowed fields.
- Dispatch publik `Str.slice(start, length)` dihapus. Helper/lowering internal bukan API publik.

## Regresi

- Native fixture mencakup exclusive/inclusive, empty/start/end, ASCII dan UTF-8 multibyte, `Some`/`None`, byte offset di tengah karakter, negative/reversed/out-of-range, stored `Range[Int32]`, serta direct/safe borrowed return dari parameter.
- Direct invalid fixtures meliputi endpoint negatif, reversed, out-of-range, mid-UTF-8, dan `INT32_MAX` inclusive pada string pendek; frontend fixtures memeriksa local-borrow escape dan penolakan `.slice` lama.
- LLVM assertions memeriksa branch validasi sebelum source-byte GEP dan widening endpoint inclusive ke `i64` sebelum increment agar `INT32_MAX` tidak wrap.

## Status verifikasi

Compiler dikonfigurasi dan dibangun pada salinan disposable; setelah pembaruan inclusive arithmetic, full suite final (`claw/tests/run_all_tests.sh`) lulus dengan exit code 0, termasuk frontend, IR/backend, native, dan kasus direct-invalid yang harus defect. Fixture range-view juga lulus ASan/UBSan/LSan dengan leak detection. Review independen read-only tidak menemukan blocker. PRD §14 tidak ditandai selesai dan file PRD tidak diubah.
