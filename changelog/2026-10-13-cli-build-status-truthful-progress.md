# Changelog: C@ Forge Truthful Build Progress

## Perubahan

Estimasi persen hard-coded dihapus dari `claw build`. Bar bertanda `=` hanya maju setelah fase selesai, `>` menunjukkan fase aktif, dan `.` menunjukkan fase yang menunggu. Profile yang dicantumkan tetap `C@ Forge | Native optimized (-O2)` karena belum ada profile debug/unoptimized atau flag pemilihannya.

LLVM lowering dan clang link tidak menyediakan callback progres, sehingga langkah akhir dilabeli indeterminate. POSIX terminal interaktif mendapat spinner; output redirected dan Windows memakai status fase statis. Pada POSIX, stderr linker ditampung selama proses link berjalan, lalu spinner dihentikan sebelum diagnostic asli diteruskan agar warning/error tidak tercampur dengan baris status.

Status sukses menandai semua fase selesai hanya setelah native builder berhasil. Kegagalan menandai fase aktif, menghentikan spinner, dan mempertahankan diagnostic serta exit code. ANSI hanya muncul di stderr terminal yang didukung ketika `NO_COLOR` tidak disetel; ANSI dinonaktifkan di Windows sebagai fallback yang aman.

## Pengujian

Tes CLI otomatis pada copy disposable memverifikasi build native sukses beserta output program, parse error, input yang tidak ditemukan, tidak adanya ANSI pada output redirected, warna PTY, `NO_COLOR` di PTY, animasi indeterminate, diagnostic linker yang tidak berimpit dengan spinner, dan status/cleanup saat linker gagal. Tes terfokus `claw/tests/cli/run_cli_status_tests.sh` dan full suite `claw/tests/run_all_tests.sh` lulus dengan exit code `0` setelah perubahan native link.
