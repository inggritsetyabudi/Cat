# Literal integer untuk PRD §11 / v0.4 (2026-10-12)

Implementasi slice literal integer yang dipilih; status PRD tidak diubah dan tidak ada perubahan pada Arena.

## Kontrak yang diimplementasikan

- Lexer mempertahankan prefiks `0x`, `0b`, `0o`, digit, underscore, dan kandidat suffix sebagai satu token integer, sambil mempertahankan perilaku desimal, float, `1.`, dan `1..5`.
- Validasi semantik menerima digit sesuai radix dan underscore hanya di antara digit yang valid. Prefiks tanpa digit, digit di luar radix, separator di awal/akhir/berulang, dan tail suffix yang tidak dikenal ditolak dengan diagnostik frontend.
- Suffix dipisahkan hanya jika teks setelah underscore tepat sama dengan nama tipe numeric yang dikenal. Karena itu `0xFF_Float32` berarti integer hex bersuffix `Float32`, sedangkan `0xAB_CD` tetap integer hex bernilai 43.981.
- Magnitudo integer dikonversi ke bentuk desimal kanonis tanpa batas parsing mesin yang lebih kecil; batas tipe target tetap diperiksa sebelum codegen. Pemeriksaan mencakup tipe expected serta tipe inferred (binding, tuple, `if`/`pick`, dan konteks payload terkait) dan tetap menerima signed minimum yang valid.
- LLVM lowering memakai magnitudo desimal ternormalisasi untuk konstanta integer. Integer hex dengan suffix float juga dikonversi melalui magnitudo integer yang sama. Validasi float desimal menolak separator yang tidak valid sebelum lowering.

## Keputusan LLVM C++ API

Tidak menambahkan dependency LLVM C++ API: `claw/CMakeLists.txt` tidak menemukan atau menautkan LLVM C++ libraries, dan backend saat ini menghasilkan LLVM IR tekstual. Parser magnitudo radix-to-decimal di helper numeric memberi jalur konversi yang terukur untuk literal ini tanpa mengubah dependency/build graph.

## Regresi dan kasus baru

- Acceptance mencakup `1_000_000`, `0xFF_UInt8`, `0xAB_CD`, `0xFF_Float32`, `1_Int8`, literal binary/octal, separator pada float, signed minimum Int8/Int128, serta maksimum UInt128.
- Diagnostik negatif mencakup prefiks radix kosong, digit binary/octal tidak valid, separator radix/decimal rusak, suffix-like tail yang bukan nama tipe, separator float berulang, overflow Int32 inferred pada binding/`if`/tuple/`pick`, overflow/underflow Int8, dan overflow UInt128.
- Test native membandingkan nilai hasil eksekusi dan memeriksa LLVM IR berisi konstanta desimal kanonis, termasuk Int128 minimum serta UInt128 maksimum.
- Suite penuh mempertahankan regresi decimal/floating `1.5`, trailing dot `1.`, range `1..5`, tuple/projection, batas numeric Int32/UInt128, dan tes native/sanitizer yang sudah ada.

## Verifikasi

Dijalankan dari salinan disposable final `/tmp/cat-v0.4-integer-literals-verified`:

```sh
cmake -S /tmp/cat-v0.4-integer-literals-verified/claw \
  -B /tmp/cat-v0.4-integer-literals-verified/build \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cat-v0.4-integer-literals-verified/build --parallel 4
CLAW_EXE=/tmp/cat-v0.4-integer-literals-verified/build/claw \
  bash /tmp/cat-v0.4-integer-literals-verified/claw/tests/run_all_tests.sh
```

Clean configure, clean build, dan seluruh suite (doc-test, frontend, OIR golden, backend, native, termasuk sanitizer checks yang tersedia) lulus dengan exit code 0. Log tersedia di `/tmp/cat-v0.4-integer-literals-verified/configure.log`, `/tmp/cat-v0.4-integer-literals-verified/build.log`, dan `/tmp/cat-v0.4-integer-literals-verified/all-tests.log`.
