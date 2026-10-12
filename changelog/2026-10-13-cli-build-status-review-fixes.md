# Changelog: C@ Forge Build Status Review Fixes

## Perbaikan

> **Catatan historis:** butir di bawah merekam iterasi review sebelum perubahan lanjutan pada [`2026-10-13-cli-build-status-direct-linker-launch.md`](./2026-10-13-cli-build-status-direct-linker-launch.md). Pada iterasi tersebut, POSIX masih menggunakan `std::system()`; itu bukan deskripsi implementasi akhir. Peluncuran `clang` saat ini memakai `posix_spawnp` langsung.

- Pada iterasi tersebut, `std::system() == -1` dilaporkan sebagai kegagalan memulai shell. Status tunggu didekode dengan `WIFEXITED`/`WEXITSTATUS` dan `WIFSIGNALED`, sehingga exit nonzero clang menyebut kode proses sebenarnya (termasuk kode 23), dan sinyal dilaporkan terpisah. Diagnostik asli tetap diteruskan dan kegagalan build CLI tetap memakai exit code `1`.
- Diagnostik stderr clang tetap dikumpulkan ke file sementara agar tidak ada deadlock pipe, lalu diteruskan dalam blok tetap 8 KiB setelah spinner berhenti. Penjaga RAII menghapus file `claw-clang-*` pada jalur sukses maupun exception/kegagalan biasa.
- Target `claw` kini meminta `Threads` secara eksplisit lewat `find_package(Threads REQUIRED)` dan menautkan `Threads::Threads`.
- POSIX `TERM=dumb` menonaktifkan ANSI. Tes PTY warna memakai `xterm-256color`; ada regresi khusus untuk `TERM=dumb`, sementara `NO_COLOR` dan stderr redirected tetap diuji.
- Pemeriksaan path input awal kini menangkap `std::filesystem::filesystem_error` dan mengakhiri status build dengan pesan kegagalan, bukan membiarkan exception filesystem lolos tanpa penanganan.

## Verifikasi

- Reproduksi baseline sebelum perbaikan: fake clang keluar `23`, CLI melaporkan keliru `Failed to launch clang`; diagnostik `simulated clang failure: exit 23` tetap terlihat dan CLI keluar `1`.
- Konfigurasi dan build CMake bersih berhasil; CMake mendeteksi `Threads: TRUE`.
- Tes CLI terfokus lulus: build Cat sukses dan output program, parse failure, input hilang, klasifikasi exit 23 yang eksplisit, cleanup file diagnostik saat fake linker sukses/gagal, diagnostik 4 MiB yang utuh, status PTY, spinner, `NO_COLOR`, `TERM=dumb`, serta pemisahan diagnostik dari spinner.
- Full suite `claw/tests/run_all_tests.sh` lulus dengan exit code `0` pada salinan disposable, sehingga artefak runner tidak mengubah worktree fitur.
- `bash -n` untuk skrip tes dan `git diff --check` lulus.
