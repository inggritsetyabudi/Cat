# Slice Int32 native pada PoC LLVM/LLD 23.1.3

Eksperimen tetap pada branch `poc/llvm-cpp-adapter-20261019`, PR #36 draft. Backend produksi tidak diganti, tidak ada fallback, dan semua opsi CMake PoC tetap default OFF. Tidak ada perubahan versi, tag, release, label PRD, atau penerimaan milestone bahasa.

## Keputusan semantik yang dipakai

[PRD §6 dan §19](../cat-northstar/PRD.md) otoritatif: Debug menangkap overflow signed sebagai defect; Release wrapping; integer divisor nol defect pada keduanya; signed MIN/-1 menghasilkan MIN untuk div dan 0 untuk rem pada Release. Kebijakan tersebut sudah diratifikasi, bukan keputusan bahasa baru.

Audit [lowering produksi](../claw/src/codegen/llvm_emit.cpp), [CLI](../docs/CLI.md#build-output-and-runtime), runtime dan [matriks penerimaan](../docs/v0.1-v0.3-acceptance-matrix.md) membedakan kebijakan itu dari implementasi. Native O0/O2 yang ada hanya profil optimisasi dan saat ini sama-sama memakai semantik release-only. Debug checked overflow masih staged v0.8. Permintaan mengikuti exact production behavior diterapkan hanya pada batas native tersebut; tidak dibuat profil safety Debug baru dan tidak diklaim seluruh kontrak Debug/Release sudah diterima.

Sebelum perubahan adapter, 22 program Cat nyata dikompilasi dan dieksekusi pada kedua profil produksi Linux: **44/44** sesuai perilaku native yang diaudit. Hasil penting sama pada O0/O2:

| Kasus | Hasil |
| --- | --- |
| MAX + 1 | -2147483648 |
| MIN - 1 | 2147483647 |
| 1073741824 * 2 | -2147483648 |
| -MIN | -2147483648 |
| -7 / 3; -7 % 3 | -2; -1 |
| MIN / -1; MIN % -1 | -2147483648; 0 |
| div/rem nol, literal/dinamis/konstanta terlipat | stdout kosong, stderr `fatal: integer division or remainder by zero; divisor must be nonzero`, Linux SIGABRT (subprocess -6) |
| divisor nol pada cabang mati/RHS short-circuit yang tidak dievaluasi | tidak trap; stdout 42 |

Probe produksi awal memakai driver produksi yang tidak diubah dan host Clang 18.1.3 sebagai toolchain referensi. Itu **bukan** SDK PoC: adapter dan LLD tetap exact 23.1.3 dengan library/probe terhubung aktual. Gate baru mengulang referensi memakai toolchain native tiap job, termasuk UCRT; hasil Linux tidak dianggap bukti Windows.

## Perubahan terisolasi

- Flag emitter `--arithmetic-i32` dan API `enableArithmeticI32=false` membuka hanya signed Int32 add/sub/mul/neg/div/rem. Flag `--runtime-i32` untuk println tetap terpisah.
- Add/sub/mul/neg wrapping tanpa LLVM `nsw` atau `nuw`. Div/rem signed memakai `sdiv`/`srem` dengan safe divisor, guard nol, hook defect C yang sudah ada, `noreturn` dan `unreachable`; MIN/-1 tidak diteruskan sebagai operasi LLVM invalid.
- Runtime C, simbol/ABI produksi dan archive target-native tidak diubah. Runtime hanya dikompilasi sekali saat preparation/package. Final per-source link tetap LLD + object + archive + CRT/SDK, bukan source Cat, LLVM IR atau runtime C melalui compiler driver.
- Tes malformed LIR dan gate native aritmetika ditambah tanpa mengubah 41 kasus/oracle lama. Default tetap menolak arithmetic. Unsigned/lebar lain, float, bitwise, owned/String dan Debug checked overflow tidak diaktifkan.

## Menjalankan bukti

Gunakan konfigurasi LLVM/LLD/runtime yang dijelaskan pada [README PoC](../claw/experimental/llvm_cpp/README.md) dan [runtime](../claw/experimental/llvm_cpp/runtime/README.md), dengan SDK exact 23.1.3. Gate tambahan otomatis terdaftar bila runtime PoC diaktifkan:

```bash
cmake --build <build> --parallel 2
ctest --test-dir <build> --output-on-failure
CLAW_EXE="$(realpath <build>/claw)" bash claw/tests/run_all_tests.sh
```

Gate `llvm-cpp-poc-arithmetic` menyiapkan seluruh referensi menggunakan `claw build <source> <exe>` (native -O0) dan `claw build --release <source> <exe>` (native -O2), lalu menjalankan emitter dengan `--runtime-i32 --arithmetic-i32` dan external LLD pada PATH terisolasi tanpa compiler. Exit, stdout/stderr, signedness/guard/overflow flags, ELF/PE, dependency, runtime immutability dan penolakan fail-closed diaudit. Command teresolusi, sumber Cat dan output aktual disimpan dalam `runtime/artifacts/arithmetic/testdata.json` dan artefak CI.

Workflow PoC tetap menguji Linux Release dan sanitizer host, serta Windows UCRT native; workflow produksi OFF, PTY dan diagnostik baseline tetap utuh. Keberhasilan harus dinilai pada exact HEAD PR, bukan diwarisi dari SHA sebelumnya. Hasil CI/review/timing/ukuran final disampaikan bersama exact SHA; dokumen ini tidak memprediksi kelulusan. Bila diagnostik baseline Windows kembali menghasilkan `0xC0000005` alih-alih oracle stack-overflow, kegagalan itu dilaporkan terpisah, bukan diperbaiki dengan melonggarkan oracle.

## Batas yang tetap terbuka

Ini bukan implementasi Debug safety Cat, bukan penerimaan seluruh integer/unsigned arithmetic, dan bukan compiler tanpa LLVM/linker/CRT/SDK. Sanitizer host C++ tidak otomatis menginstrumentasi archive runtime C atau executable target. String/ownership tidak diperluas. Optimasi IR, footprint dependency LLVM/LLD statik dan regresi performa yang pernah diukur tetap perlu dilaporkan jujur; waktu emit/link contoh kecil bukan klaim kemenangan performa umum. Tidak ada promosi jalur produksi tanpa keputusan terpisah.
