# ABI printer native `Int8` signed (2026-10-13)

Perbaikan sempit untuk ABI parameter runtime `print(Int8)` dan `println(Int8)`. Source runtime C dan printer `UInt8` tidak diubah.

## Reproduksi sebelum perubahan

Program Cat yang memanggil `println` untuk `-128_Int8`, `-1_Int8`, `0_Int8`, dan `127_Int8` berhasil dibangun, tetapi output native aktualnya `128`, `255`, `0`, `127`. LLVM IR mendeklarasikan dan memanggil `claw.runtime.println.i8` dengan `i8` tanpa atribut ekstensi; ABI C penerima memakai `int8_t` signed.

## Perbaikan

- `llvmRuntimeParamType("Int8")` kini menghasilkan `i8 signext`, konsisten pada deklarasi runtime dan callsite `print` maupun `println`.
- `UInt8` tetap menggunakan `i8 zeroext`; simbol `.u8`, kode runtime, serta ABI tipe lain tidak berubah.

## Regresi

- Fixture native baru `revise_int8_print.cat` menguji `print(-128)`, `print(-1)`, `print(0)`, dan `println(127)` dengan byte output exact `-128,-1,0,127\n`.
- Fixture `revise_int8_range.cat` juga mencetak item signed dari scan biasa/berindeks serta tepi full range `-128` dan `127`.
- Backend LLVM assertions memverifikasi `signext` di deklarasi dan callsite `print`/`println`, serta ketiadaan `zeroext` untuk `Int8`. Tes `UInt8` tetap memverifikasi `zeroext`.

## Verifikasi

- Sebelum perbaikan, native Cat repro menghasilkan `128`, `255`, `0`, `127` dan IR tidak memuat `signext`.
- Sesudah perbaikan, output exact menjadi `-128,-1,0,127`; LLVM memuat `i8 signext` pada deklarasi/callsite.
- Native print dan Range[Int8] fixture lulus ASan/UBSan/LSan dengan leak detection; focused backend/native tests dan full suite pada disposable copy lulus (`FULL_SUITE_EXIT=0`).
