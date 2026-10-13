# `Range[UInt8]` dan scan unsigned (2026-10-13)

Slice terbatas PRD §7 ini menambahkan konstruksi/inferensi `Range[UInt8]`, membership `UInt8 in Range[UInt8]`, scan tanpa indeks dan dengan indeks `USize`. Slicing `Str` tetap hanya menerima `Range[Int32]`; status PRD tidak diubah dan Arena tidak disentuh.

## Baseline sebelum perubahan

Pada fresh `origin/main` `ad6ac3f86726395d00babc4f6b1c5996148086f8`, compiler berhasil dibangun out-of-tree dengan CMake/Ninja (`cmake -S claw -B /tmp/cat-u8-baseline-build -G Ninja -DCMAKE_BUILD_TYPE=Debug`). Empat program Cat nyata dijalankan memakai `claw check` sebelum source diubah:

- Contoh PRD `scan n over 0..=255_UInt8 { ... }` ditolak karena bound dilihat sebagai `Int32` dan `UInt8`.
- Tipe `Range[UInt8]` dan membership ditolak karena yang didukung hanya `Range[Int32]` dan `Range[Char]`.
- Scan biasa dan scan berindeks juga ditolak pada konstruksi range atau validasi indexed scan.

## Implementasi

- Sema menerima `UInt8` sebagai satu-satunya width integer tambahan untuk Range. Anotasi `Range[UInt8]`, literal bersuffix, serta inferensi `0..=255_UInt8` didukung; pasangan bound dan operand `in` harus bertipe sama. `Int8`, `UInt16`, dan width lain tetap unsupported.
- Representasi native `Range[UInt8]` adalah `{ i8, i8, i1 }`; `Range[Int32]` dan `Range[Char]` mempertahankan layout `{ i32, i32, i1 }`.
- Membership dan batas scan memakai predicate unsigned (`uge`, `ult`, `ule`) untuk `UInt8`. Iterator menyimpan current/end sebagai `i8`, index sebagai `USize` mulai 0, dan menandai item inclusive terakhir selesai sebelum increment. Akibatnya `0..=255_UInt8` menghasilkan 256 item tanpa wrap; exclusive dekat maksimum, empty, equal, dan reversed ranges juga tercakup.
- Endpoint range dievaluasi sekali saat konstruksi. Native runtime mendapat printer `.u8` khusus untuk `print`/`println` agar nilai item 254/255 tetap unsigned; jalur `.i8` untuk `Int8` tidak berubah.
- `Str` slicing tetap dibatasi `Range[Int32]`; status §7 tidak dipromosikan. Tidak ada perubahan pada Arena.

## Regresi

- Native fixture memeriksa inferensi dan anotasi, membership batas, perbandingan unsigned, scan biasa/berindeks, index awal 0, endpoint ber-efek samping yang hanya dievaluasi sekali, kasus empty/equal/reversed, `..`/`..=`, serta penyelesaian inclusive pada 255.
- Frontend/OIR menolak width `UInt16`, pasangan bound dan membership campuran, indexed scan `Str`, dan slicing `Str` dengan `Range[UInt8]`.
- Golden OIR dan backend LLVM mengunci ABI `i8`, unsigned compare, increment sempit, indeks `i64`, serta pemilihan runtime printer unsigned.

## Verifikasi

- Build compiler baseline/final out-of-tree berhasil: `cmake --build /tmp/cat-u8-baseline-build --target claw -j 4`.
- Baseline repro Cat dilakukan sebelum edit source; setelah implementasi, fixture UInt8 lolos `claw check`, OIR/LLVM lowering, native build, dan eksekusi berpagar 10 detik. Output cocok persis dengan 29 baris ekspektasi, termasuk nilai `254`, `255`, index `0`/`1`, dan hitungan 256 untuk scan penuh.
- Full suite (`bash claw/tests/run_all_tests.sh`, termasuk doc-test, frontend, OIR, backend LLVM, dan native) lulus dengan exit code 0 pada copy disposable `/tmp/cat-u8-final-suite.BZq1in`; artefak runner tidak masuk worktree.
- Native fixture UInt8 juga lulus ASan/UBSan/LSan (`detect_leaks=1`, `halt_on_error=1`) dan stdout tersanitasi identik byte demi byte dengan hasil normal.
