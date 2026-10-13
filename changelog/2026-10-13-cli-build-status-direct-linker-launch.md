# Peluncuran linker POSIX tanpa shell

Build native kini menjalankan `clang` langsung melalui `posix_spawnp` dan menunggu prosesnya dengan `waitpid`, bukan membentuk perintah shell melalui `std::system()`. Path IR, runtime, dan output diteruskan sebagai argumen terpisah; spasi maupun karakter khusus shell tidak diinterpretasikan sebagai sintaks shell. Flag native yang ada, termasuk `-O2 -std=c11`, tetap dipertahankan. Cabang Windows `_wspawnvp` tidak berubah.

Pada baseline sebelum perbaikan, tidak ditemukannya clang di `PATH` menghasilkan pesan `clang exited with status 127`, sedangkan fake clang yang dihentikan oleh SIGILL menghasilkan `clang exited with status 132`. Keduanya adalah status shell, bukan klasifikasi yang dapat dipercaya atas proses compiler. Dengan peluncuran langsung, kegagalan pencarian/eksekusi, kegagalan membuat atau menunggu proses, exit normal clang, dan terminasi oleh sinyal dibedakan. Diagnostik clang tetap ditangkap ke file sementara, diteruskan dalam potongan terbatas setelah spinner berhenti, dan file sementara dibersihkan lewat RAII.

Tes CLI mencakup clang yang hilang (`ENOENT`), terminasi `SIGILL`, exit normal palsu `23`, diagnostik 4 MiB, path output dengan spasi dan sintaks yang akan menyuntikkan perintah shell, keberhasilan build, status TTY/warna, serta pembersihan file diagnostik.

## Validasi

- Reproduksi baseline: clang tidak ada di `PATH` menghasilkan `clang exited with status 127`; fake clang dengan SIGILL menghasilkan `clang exited with status 132`. Dalam kedua kasus, CLI mempertahankan exit `1`, tetapi klasifikasi linker sebelumnya keliru.
- Konfigurasi dan build CMake baru di salinan sementara menemukan `Threads` dan membangun target `claw` dengan sukses.
- Tes CLI terfokus dan seluruh rangkaian `claw/tests/run_all_tests.sh` lulus pada salinan disposable.
- Cat valid menghasilkan exit `0` dan stdout `Built native executable: ...`; Cat tidak valid mempertahankan exit `1`, diagnostik parse, status gagal, dan stdout kosong.
- `bash -n claw/tests/cli/run_cli_status_tests.sh` dan `git diff --check` lulus. Worktree fitur tidak menerima artefak build; perubahan tetap belum di-stage.
