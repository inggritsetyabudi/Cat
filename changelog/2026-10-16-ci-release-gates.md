# CI lintas platform untuk gerbang rilis — 2026-10-16

## Perubahan

- Menambahkan workflow GitHub Actions pada `pull_request` dan push ke `main`, dengan job bukti terpisah untuk Linux dan Windows MSYS2 UCRT64. Kedua job melakukan konfigurasi CMake/Ninja Release dari direktori build bersih, membangun target `claw` memakai Clang, lalu menjalankan `claw/tests/run_all_tests.sh` penuh.
- `CLAW_EXE` ditetapkan eksplisit pada kedua platform. Job Windows memasang paket MSYS2 `python` untuk PRD doc-test (`python3`) dan `diffutils` untuk pembandingan golden test (`diff`/`cmp`).
- Menormalkan CRLF pada output teks AIR/OIR dan native sebelum pembandingan lintas-platform; golden/value assertions dan pemeriksaan `cmp` tetap ada. Frontend dan native runner memakai `/usr/bin/mktemp` MSYS2 serta memvalidasi direktori; frontend juga punya fallback terdiagnostik ke direktori test workspace. Empat rejection test native menangkap diagnostic secara in-memory, mempertahankan pengecekan pesan dan ketiadaan binary/LLVM tanpa bergantung pada file log sementara. Tidak ada fixture, assertion, atau suite yang dihapus/dilewati.
- Izin workflow dibatasi ke `contents: read`; action pihak ketiga dipin ke commit SHA; tidak ada secret yang diberikan ke job. Build/test artifacts hanya berada di workspace runner sementara dan tidak diunggah.

## Validasi

- Baseline `b756b987a67c7ea449d76f9eedc4e2f6ce9a60a9`: konfigurasi/build Release bersih dan `bash claw/tests/run_all_tests.sh` lulus di Linux pada worktree sementara. Toolchain: Clang 18.1.3, CMake 3.28.3, Ninja 1.11.1. Workflow diperiksa dengan `actionlint` v1.7.12.
- PR run `38903409953`: Linux full suite lulus; Windows setup, configure, build, dan PRD doc-test lulus, tetapi frontend gagal karena `diff` belum tersedia. Paket MSYS2 `diffutils` ditambahkan. Run `38903807164` kemudian meluluskan Linux dan menemukan CRLF pada golden output Windows; pembandingan dinormalisasi tanpa mengurangi cakupan.
- PR run `38905106932` dan `38906260424`: Linux full suite lulus. Windows clean build dan PRD doc-test lulus, tetapi frontend gagal saat membuka fixture sementara di path hasil `mktemp`; template eksplisit saja belum menyelesaikannya. Frontend kemudian diikat ke `/usr/bin/mktemp` dan mendapatkan guard/fallback.
- PR run `38907214696`: Linux full suite lulus. Windows melewati PRD doc-test, frontend, OIR, dan backend, lalu gagal di native karena temp path yang dihasilkan pemanggilan `mktemp` tanpa path eksplisit tidak tersedia. Native runner diikat ke `/usr/bin/mktemp` dan diberi pemeriksaan direktori.
- PR run `38907807361` dan `38908648276`: Linux full suite lulus. Windows melewati PRD, frontend, OIR, backend, dan sebagian besar native; keduanya berhenti pada pemeriksaan diagnostic fixed-array overflow karena file log expected tidak dapat dibaca. Owner-PID cleanup guard sempat diuji pada `38908648276` tetapi tidak mengubah titik gagal, sehingga tidak dipertahankan. Perubahan terbaru menghilangkan file log sementara pada empat rejection test terkait dan tetap memeriksa diagnostic yang sama serta output yang dilarang. `bash -n` dan `git diff --check` lulus; rerun Windows penuh masih menunggu.
- Workflow belum menjadi required check/branch protection di `main`. Pemeriksaan permanen pada branch default memerlukan workflow berada di `main` dan run setelah itu; PR ini tidak mengubah branch protection.

## Batas rilis

Perubahan ini hanya menambah bukti CI lintas-platform; tidak mengubah fitur bahasa/compiler, nomor versi, tag, branch protection, atau menerbitkan release. Ini **bukan** klaim production-ready maupun rilis v1.0. Gerbang PRD berikutnya tetap mencakup borrow checking dataflow v0.7, benchmark/optimasi v0.8 (selisih maksimum 10% terhadap C `clang -O3`), dan fuzzing/pengerasan serta audit Arena v0.9 sebelum gerbang v1.0.
