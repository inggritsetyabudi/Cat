# Wide `Range` stored-layout alignment (2026-10-14)

## Temuan dan perbaikan

Review menemukan bahwa fallback LLVM ABI layout hanya mengenali `Range` lama. Range yang baru didukung akibatnya dibuat dan disimpan dengan alignment `1`, walaupun elemen terbesarnya memerlukan alignment alami `2`, `4`, atau `8` byte. Probe baseline juga menunjukkan tuple yang berisi `Range` gagal dilower karena layout element tidak tersedia.

`abiLayoutForType` kini menghitung layout aggregate `{ T, T, i1 }` dari layout elemen untuk seluruh tipe Range yang didukung. Pada target 64-bit, ukuran/alignment yang diharapkan adalah `Range[UInt16]` 6/2, `Range[UInt32]` 12/4, serta `Range[Int64]`, `Range[UInt64]`, dan `Range[USize]` 24/8 byte. Bentuk field tetap start, end, lalu flag inclusive; alignment hasil mencakup tail padding.

Regresi memeriksa field indices 0/1/2, alignment alloca/store untuk Range tersimpan, serta nested tuple `{ { i16, i16, i1 }, i32 }` pada alignment 4 dan `{ { i64, i64, i1 }, i16 }` pada alignment 8. Native test membaca field tuple 1 secara exact untuk memverifikasi akses field setelah Range.

## Validasi

CMake build dengan Clang 18, backend/native suites terfokus, dan full `claw/tests/run_all_tests.sh` lulus di disposable copy. Runner otomatis mengompilasi dan menjalankan fixture Range wide serta nested tuple di bawah ASan, UBSan, dan LSan; keduanya lulus dengan output expected.
