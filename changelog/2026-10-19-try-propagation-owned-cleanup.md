# Cleanup owned pada propagasi gagal shorthand `try`

## Bug yang diperbaiki

Pada main `f8816eb6a8addcf70801bc5fc3469f1eecf4bab6`, local `String.from("scan-guard")` bocor 10 byte ketika `val value = try make_failure()` meneruskan `Fail(String)` keluar fungsi. Kasus minimal tanpa `scan` tetap bocor, sedangkan kontrol dengan `else error { return Fail(error) }` bebas leak. Ini melanggar kontrak PRD §13 bahwa setiap alokasi owned dibebaskan tepat sekali; tidak memerlukan keputusan semantik baru.

Reproducer permanen: `claw/tests/integration/fixtures/try_cleanup_minimal.cat`. Jalur yang benar mencetak `failure` lalu `after`, bukan guard. Pada compiler baseline bersih, target ASan/UBSan/LSan O0 dan O2 keluar 1 dengan laporan `10 byte(s) leaked in 1 allocation(s)`; kontrol manual keluar 0. Gate integrasi baru benar-benar gagal di baseline karena laporan itu. Oracle `free` tanpa sanitizer juga menunjukkan cleanup guard yang hilang, jadi regresi tidak bergantung pada ketersediaan runtime sanitizer Windows.

## Perubahan implementasi

- Ownership checker mengumpulkan cleanup seluruh fungsi pada shorthand, **setelah** konsumsi ekspresi Result dan **sebelum** binding Ok diperkenalkan. Daftar ini khusus jalur gagal; state jalur sukses tidak diubah.
- OIR menurunkan shorthand ke blok gagal eksplisit melalui mekanisme `try ... else` yang sudah ada. Payload error ditransfer ke `Fail` bertipe return fungsi, lalu cleanup owner aktif dijalankan sebelum return. Binding key dan urutan unwind yang sudah ada dipertahankan; Result yang sudah dipindahkan tidak ikut didrop.
- Tidak ada perubahan runtime produksi, backend LLVM, syntax, versi, tag/release, acceptance marker PRD, Arena, f-string, atau Float128.

## Regresi yang dipertahankan

Delapan fixture integrasi menjalankan `check`, emisi LLVM, native build/run, dan relink LLVM O0/O2 biasa serta ASan/UBSan/LSan dengan stdout exact. Linux mewajibkan runtime sanitizer, dengan `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` dan `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

- Minimal shorthand dan kontrol manual memeriksa output error serta empat eksekusi tambahan dengan runtime oracle test-only. Oracle mengamati `free` nyata dan tetap memanggil libc `free`: cleanup guard harus terjadi sebelum caller mencetak error, cleanup error setelah pemakaiannya, masing-masing sekali. Runtime produksi tetap diuji terpisah dengan sanitizer.
- Fixture owned Result/choice, scan/loop/pick/skip/stop dari workstream test-only sebelumnya dipertahankan sebagai regresi aktif, tanpa memakai branch itu sebagai baseline.
- Matriks tambahan mencakup owned parameter, local yang sudah dipindahkan, konsumsi Result lokal, success type pemanggil berbeda, shadowing/nested scope, owner dalam cabang, chained try, shape/choice berisi Anchor[String], borrowed view, dan early exits.
- Golden OIR minimal mensyaratkan cleanup pada blok gagal dan return normal. Tiga golden lama diperbarui hanya untuk representasi CFG shorthand yang kini eksplisit; tidak ada assertion yang dihapus atau dilemahkan.

## Validasi

Reproducer minimal serta kontrol manual sudah lolos target native dan ASan/UBSan/LSan O0/O2 setelah fix, dengan stderr sanitizer kosong. Coverage OIR dan backend terkait juga lolos. Percobaan native suite awal berhenti karena linker kehabisan ruang disk; kegagalan lingkungan itu bukan PASS dan harus diulang dalam validasi bersih.

Validasi lengkap untuk commit final dicatat pada review/PR: clean full Release dan clean full host ASan/UBSan/LSan suite dalam tree disposable terpisah, independent reviewer yang build/run exact commit, serta CI Linux Release, Linux sanitizers, dan Windows UCRT64 pada head/base terkini. Jumlah target pada gate integrasi dengan sanitizer tersedia adalah 44, ditambah companion negatif 52 pemeriksaan dan pattern oracle 100 kasus. Lulusnya kasus-kasus ini bukan bukti seluruh PRD selesai atau compiler bebas semua bug. Workstream ini diajukan sebagai draft PR, tanpa merge otomatis.
