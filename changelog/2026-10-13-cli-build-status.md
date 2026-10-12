# Changelog: C@ Forge Build Status

## Perubahan awal

Command `claw build` memperoleh label khas **C@ Forge** dan status untuk tahapan membuka source, memuat graph paket, pemeriksaan tipe/entry, ownership, lalu LLVM/native link. Implementasi awal memberi angka persentase tetap pada beberapa batas fase. Angka itu merupakan estimasi manual, bukan ukuran unit compiler atau linker yang selesai; koreksi akurasinya dicatat pada [`2026-10-13-cli-build-status-truthful-progress.md`](./2026-10-13-cli-build-status-truthful-progress.md).

Status sukses dan gagal ditampilkan ke stderr; baris sukses `Built native executable: ...`, diagnostik compiler, serta exit code sukses `0` dan gagal `1` tetap dipertahankan.

## Batas profile

CLI saat ini tidak menyediakan pilihan debug/unoptimized maupun flag untuk memilih profile. Native link masih selalu memanggil `clang -O2`, sehingga label menyebut perilaku aktual `Native optimized (-O2)` dan tidak mengklaim bahwa profile release/debug dapat dipilih.

## Reproduksi baseline awal

- `claw build` pada fixture Cat `revise_maybe.cat` berhasil dengan exit code `0`; executable menghasilkan `42`, `0`, `C@`, dan `world`. Output CLI baseline sebelum fitur ini hanya `Built native executable: ...`, tanpa status/progres.
- `claw build` pada fixture `revise_bad_char_empty.cat` gagal dengan exit code `1` dan diagnostik parse karakter kosong, tanpa ringkasan status.
