# Literal Char Unicode untuk PRD §11 / v0.4 (2026-10-12)

Implementasi literal `Char` dari lexer sampai executable native. Status PRD tidak diubah dan Arena tidak disentuh.

## Kontrak yang diimplementasikan

- Lexer mendekode satu UTF-8 Unicode scalar value untuk setiap literal karakter; scalar valid berada pada U+0000–U+10FFFF dan mengecualikan surrogate U+D800–U+DFFF.
- Escape `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\0`, dan `\u{...}` menghasilkan scalar yang sama seperti karakter UTF-8 mentah.
- Literal kosong, lebih dari satu scalar, tanpa penutup, escape tidak dikenal/malformed, escape surrogate/di atas U+10FFFF, dan encoding UTF-8 invalid menghasilkan diagnostik frontend. Error literal mempertahankan span token sumber, termasuk panjang byte UTF-8.
- AST menyimpan nilai codepoint 32-bit pada `CharExpr`; sema mengaitkannya dengan tipe builtin `Char`. AIR/OIR membawa nilai scalar numerik, LLVM menurunkannya ke `i32`, dan global `static Char` memakai initializer scalar yang sama.
- Pemetaan LLVM `Char` ke `i32` sudah tersedia di backend tekstual. Tidak ditambahkan LLVM C++ API atau dependency baru.

## Regresi dan kasus baru

- Fixture frontend menerima ASCII `A`, multibyte `é`, escape emoji, NUL, newline/tab/CR, quote tunggal/ganda, backslash, U+D7FF, U+E000, dan U+10FFFF.
- Tes negatif mencakup literal kosong/multiple scalar, escape tidak dikenal dan Unicode malformed, surrogate, codepoint di atas U+10FFFF, literal tidak tertutup, serta UTF-8 illegal lead, overlong 2/3-byte, truncated 2/3-byte, continuation rusak, surrogate encoding, dan encoding di atas U+10FFFF.
- Tes frontend memeriksa AIR memuat codepoint numerik bertipe `Char` dan diagnostik empty literal menunjuk ke span `:2:15` dengan dua caret.
- Tes native membandingkan ASCII dan karakter UTF-8 mentah `é`/`😀` dengan escape Unicode; turut menguji NUL, whitespace escapes, quote, backslash, scalar batas, dan `static Char`. LLVM IR menunjukkan konstanta `i32 233`, `128512`, `55295`, `57344`, dan `1114111`; executable selesai dengan exit code 0 dan runner menjalankan ulang fixture Char di bawah ASan/UBSan/LSan bila tersedia.
- Regresi decimal/floating/integer/string/comment/identifier tetap dijalankan oleh suite penuh yang ada.

## Baseline

Sebelum perubahan, branch dedicated berada bersih pada `3ed48a0eedb354919bbd9ed6270879b055be834f`. Repro ulang dengan compiler baseline di `/tmp/cat-char-baseline-build/claw` menolak `'A'`, `'é'`, dan `\u{1F600}` dengan `error[parse]: Expected expression.` sebagaimana expected baseline.

## Verifikasi

Dikerjakan pada salinan disposable final `/tmp/cat-char-literals-20261012-reviewed-final`:

```sh
cmake -S /tmp/cat-char-literals-20261012-reviewed-final/claw \
  -B /tmp/cat-char-literals-20261012-reviewed-final/build \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18
cmake --build /tmp/cat-char-literals-20261012-reviewed-final/build --parallel 4
(cd /tmp/cat-char-literals-20261012-reviewed-final/claw && \
  CLAW_EXE=/tmp/cat-char-literals-20261012-reviewed-final/build/claw \
  bash tests/run_all_tests.sh)
```

Clean configure dan build memakai Clang 18.1.3/Ninja lulus; `bash claw/tests/run_all_tests.sh` lulus seluruh doc-test, frontend, OIR, backend, native, dan sanitizer checks yang tersedia. Log: `configure.log`, `build.log`, dan `all-tests.log` pada direktori disposable final.

Executable Char juga dikompilasi ulang dari LLVM IR dengan `clang-18 -fsanitize=address,undefined,leak -O1 -g`; eksekusi berakhir sukses dengan ASan/UBSan/LSan aktif dan tanpa laporan sanitizer. Tidak ada perubahan PRD, status bagian, atau Arena.
