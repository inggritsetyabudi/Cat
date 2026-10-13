# ABI runtime printer `UInt8` dan cakupan `print` (2026-10-13)

Perbaikan lanjutan untuk slice `Range[UInt8]`: panggilan runtime `print(UInt8)` dan `println(UInt8)` kini mengikuti ABI C untuk `uint8_t`. Clang menyatakan parameter tersebut dengan atribut LLVM `zeroext`; atribut ini harus hadir pada deklarasi eksternal dan setiap callsite agar pelebaran argumen konsisten di semua target.

Perubahan hanya menambahkan `zeroext` untuk argumen runtime bertipe `UInt8`. Jalur `Int8` dan tipe lain tidak diubah.

## Regresi

- Backend memeriksa atribut `zeroext` pada deklarasi dan callsite `.println.u8` serta `.print.u8`.
- Native fixture memanggil `print` untuk nilai `254_UInt8` dan `255_UInt8`, lalu membandingkan output tepat `254255` byte demi byte untuk memastikan tidak ada newline.

## Verifikasi

- Compiler berhasil dibangun dengan CMake/Ninja di `/tmp/cat-u8-abi-build`. LLVM IR melewati `llvm-as`; Clang 18 yang mengompilasi `claw/runtime/native_runtime.c` langsung menunjukkan parameter `zeroext` pada definisi `print.u8` dan `println.u8`.
- Native build Claw dan kompilasi ulang modul LLVM native lengkap melawan runtime C aktual pada `-O0` dan `-O2` semuanya menghasilkan byte output yang sama persis: `254255` tanpa newline. ASan/UBSan/LSan (`detect_leaks=1`, `halt_on_error=1`) lulus untuk fixture focused tersebut.
- Pemeriksaan IR `Int8` memastikan deklarasi dan call `.println.i8` tetap `i8` tanpa `zeroext`; jalur Int8 tidak diubah.
- Full suite `bash claw/tests/run_all_tests.sh` lulus dengan exit code 0 pada salinan disposable `/tmp/cat-u8-abi-suite.C4EHVX`, termasuk tes backend ABI dan native print baru. Artefak suite berada di salinan tersebut, bukan worktree.
- `git diff --check` bersih; tidak ada perubahan yang di-stage.