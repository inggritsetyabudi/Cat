# `Range[Char]` dan scan Unicode scalar (2026-10-12)

Irisan implementasi khusus PRD §7. Compiler kini menerima konstruksi dan anotasi `Range[Char]`, membership `Char in Range[Char]`, serta scan biasa dan scan berindeks dengan indeks `USize`. §7 tetap parsial; bagian slicing §14 tetap menggunakan `Range[Int32]` untuk `Str`. Tidak ada perubahan pada status PRD, Arena, atau rentang integer selain `Int32`.

## Implementasi

Sema menginferensikan rentang dari dua endpoint bertipe sama (`Int32` atau `Char`), menerima anotasi `Range[Char]`, dan menolak endpoint atau pasangan membership yang mencampur tipe. Tipe integer range selain `Int32` tetap tidak didukung.

Range tetap memakai representasi native yang ada (dua endpoint i32 dan flag inklusif). Membership mempertahankan batas kiri inklusif dan endpoint kanan sesuai `..` atau `..=`. Scan `Char` menyimpan state i32 dengan indeks `USize` 64-bit mulai dari 0; indeks bertambah per scalar yang dikeluarkan dan successor U+D7FF langsung menjadi U+E000. Item inclusive terakhir menandai iterator selesai sebelum langkah successor, termasuk pada U+10FFFF. Nilai endpoint dibentuk sekali saat konstruksi range; iterator tidak mengevaluasi ulang ekspresi endpoint. Lowering slicing `Str` tetap dibatasi pada `Range[Int32]` dan memiliki regresi negatif untuk `Range[Char]`.

## Regresi permanen

`claw/tests/native/fixtures/revise_char_range.cat` menguji membership exclusive/inclusive, rentang kosong/terbalik/setara, scan biasa dan berindeks, index awal nol, range tersimpan, endpoint ber-efek samping, lintasan surrogate, dan terminasi inclusive U+10FFFF. Frontend fixtures memeriksa tipe anotasi `Range[Char]`, penolakan endpoint/membership campuran, penolakan slicing `Str` dengan `Range[Char]`, dan penolakan `Range[Int64]`. Backend memeriksa representasi LLVM/successor scalar, sedangkan native test memeriksa output persis dengan timeout. Regresi `Range[Int32]` yang sudah ada tetap menjadi kontrol.

## Verifikasi

- Konfigurasi dan build CMake/Ninja langsung pada worktree berhasil: `cmake -S claw -B /tmp/cat-char-range-build -G Ninja -DCMAKE_BUILD_TYPE=Debug` dan `cmake --build /tmp/cat-char-range-build --target claw -j 4`.
- Pemeriksaan Cat fokus untuk fixture Char positif, anotasi tipe, kontrol `Range[Int32]`, serta diagnostik untuk bounds/membership campuran, `Range[Int64]`, integer widths lain, dan float berhasil sesuai ekspektasi. Native Char executable selesai dalam batas 10 detik dan outputnya cocok byte-for-byte.
- `bash claw/tests/run_all_tests.sh` berhasil seluruhnya (doc, frontend, OIR, backend LLVM, dan native executable), dijalankan dengan `CLAW_EXE=/tmp/cat-char-range-build/claw` dalam salinan disposable agar artefak runner tidak mengotori worktree.
- Native boundary fixture juga lulus kompilasi dan eksekusi AddressSanitizer + UndefinedBehaviorSanitizer (`detect_leaks=1`, `halt_on_error=1`); output tersanitasi cocok persis.
