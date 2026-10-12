# Validasi Scalar pada Integer-to-Char (2026-10-12)

Follow-up untuk literal `Char` v0.4: konversi integer bertarget `Char` sekarang mengikuti kontrak Unicode scalar PRD §11, tanpa mengubah PRD, status, atau Arena.

## Perubahan

- Pemeriksaan bersama `integerLiteralFitsTarget` menerima U+0000–U+10FFFF kecuali rentang surrogate U+D800–U+DFFF. Dengan demikian initializer bertipe eksplisit, suffix `_Char`, initializer `static`, dan jalur contextual/inferred yang relevan mendapat validasi batas yang sama.
- Unary negatif bertarget `Char` hanya menerima magnitude nol (`-0`); nilai negatif nonnol ditolak pada konteks typed dan suffix yang mendukungnya. Aturan static initializer tidak diperluas: unary expression tetap bukan literal static yang diizinkan.

## Regresi

- Fixture positif menguji integer 0, U+D7FF, U+E000, dan U+10FFFF melalui initializer typed, suffix, dan static; juga memastikan `-0` diterima pada typed dan suffix.
- Fixture negatif menguji U+D800 dan U+DFFF pada initializer typed, suffix `_Char`, dan static; menguji U+110000 pada typed, suffix, dan static; serta `-1` pada typed dan suffix.
- Seluruh fixture didaftarkan pada frontend runner dan memeriksa diagnostik `does not fit target type Char.` untuk kasus invalid.

## Baseline dan verifikasi

- Compiler yang dibangun dengan validator sebelum follow-up menerima fixture surrogate bertarget typed, suffix, dan static. Compiler baru menolak semuanya dengan diagnostik semantik; fixture batas scalar valid lulus.
- Clean configure/build pada salinan disposable dengan CMake, Ninja, dan Clang 18 lulus. `bash tests/run_all_tests.sh` lulus doc-test, frontend, OIR, backend, native, serta pemeriksaan ASan/UBSan/LSan yang tersedia, termasuk regresi native Char.
- Review independen read-only berstatus **READY**, tanpa blocker atau bypass pada jalur integer-to-Char yang ditinjau.
- Tidak ada perubahan pada PRD, status bagian, Arena, atau perubahan eksternal; tidak ada stage, commit, push, maupun PR.
