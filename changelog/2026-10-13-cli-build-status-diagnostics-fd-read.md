# 2026-10-13 — Replay diagnostik clang dari descriptor terbuka

Native linker kini menyimpan descriptor tempfile diagnostik clang tetap terbuka hingga seluruh output dibaca kembali. Replay menggunakan `pread` dalam potongan tetap 8 KiB, sehingga buffer memori terbatas dan pembacaan tidak bergantung pada pembukaan ulang pathname setelah clang selesai. Ini menghindari pembacaan file lain jika pathname tempfile diganti ketika proses linker masih berjalan.

Tes CLI menambahkan fake-linker yang menulis diagnostik melalui stderr yang diwariskan, menghapus tempfile bernama itu, lalu membuat file pengganti berisi marker jebakan. Tes memastikan marker diagnostik dari descriptor asli diteruskan, marker file pengganti tidak muncul, status keluar clang tetap terklasifikasi, dan cleanup tempfile tetap berlangsung.

Validasi yang sudah lulus:

- Build CMake Release segar dari sumber worktree ke `/tmp/claw-cli-build-status-fd-verify`.
- Suite CLI terfokus dengan compiler hasil build tersebut, termasuk payload diagnostik 4 MiB, exit 23, clang tidak ditemukan, SIGILL, stderr tertutup, cleanup, penggantian pathname descriptor, dan mode PTY/TTY.
- Suite penuh (`bash claw/tests/run_all_tests.sh`) pada salinan disposable `/tmp/claw-full-suite.X83ypQ`; suite dokumentasi, frontend, OIR, backend, native, dan CLI semuanya lulus.
- `bash -n` untuk runner CLI dan suite penuh.
- `git diff --check`.

Perubahan ini hanya memperkeras pembacaan diagnostik linker; semantik compiler dan pilihan profil build tidak berubah.
