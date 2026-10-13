# Changelog: C@ Forge Temporary Diagnostics Hardening

## Perubahan akhir

- Di POSIX, descriptor eksklusif yang dibuat `mkstemp` kini tetap terbuka dan langsung dipasangkan ke stderr clang melalui `posix_spawn_file_actions_adddup2`; path tidak lagi dibuka ulang untuk menyiapkan spawn. Aksi close di child hanya ditambahkan bila descriptor berbeda dari `STDERR_FILENO`. Jika stderr parent sebelumnya tertutup dan `mkstemp` memakai nomor descriptor itu, descriptor dipindahkan lebih dulu agar status parent tidak masuk ke file diagnostik.
- Pemilik RAII mempertahankan descriptor dan pathname sampai diagnostik selesai diteruskan, lalu menutup descriptor serta menghapus file pada jalur sukses maupun exception. Kegagalan saat pembentukan objek juga membersihkan file dan descriptor yang sudah teralokasi.
- Hasil `posix_spawn_file_actions_destroy` kini diperiksa pada jalur setup, spawn, wait, dan cleanup tak terduga. Kegagalan cleanup dilaporkan sebagai catatan/peringatan tanpa menggantikan status utama clang atau kegagalan spawn.
- Catatan [`2026-10-13-cli-build-status-review-fixes.md`](./2026-10-13-cli-build-status-review-fixes.md) tetap dipertahankan sebagai histori iterasi sebelum direct `posix_spawnp`; implementasi final dijelaskan pada changelog peluncuran linker dan catatan hardening ini.

## Validasi

- Baseline sebelum edit dibangun di direktori sementara dan suite CLI terfokus lulus; test mencakup fake linker exit 23, clang tidak ditemukan, SIGILL, stderr 4 MiB, serta cleanup tempfile.
- Setelah perubahan, fresh CMake configure/build dan suite CLI terfokus lulus. Kasus yang diuji mencakup success/error parse, input hilang, exit 23, clang hilang, stderr parent tertutup, SIGILL, argumen path shell-sensitive, diagnostics 4 MiB, cleanup tempfile, PTY berwarna, `NO_COLOR`, `TERM=dumb`, dan pemisahan spinner/diagnostic.
- `claw/tests/run_all_tests.sh` lulus dengan exit code `0` pada salinan disposable, termasuk PRD doc-test, frontend, OIR, LLVM backend, native, serta CLI.
- `bash -n` dan `git diff --check` lulus. Build dilakukan di luar worktree fitur; tidak ada build artifact yang ditambahkan di sana.
