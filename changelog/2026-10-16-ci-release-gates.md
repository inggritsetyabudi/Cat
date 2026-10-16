# CI lintas platform untuk gerbang rilis — 2026-10-16

## Perubahan

- Menambahkan workflow GitHub Actions pada `pull_request` dan push ke `main`, dengan job bukti terpisah untuk Linux dan Windows MSYS2 UCRT64.
- Kedua job melakukan konfigurasi CMake/Ninja Release dari direktori build bersih, membangun target `claw` memakai Clang, lalu menjalankan `claw/tests/run_all_tests.sh` penuh.
- `CLAW_EXE` ditetapkan eksplisit pada kedua platform. Job Windows juga memasang paket MSYS2 `python`, yang menyediakan `python3` untuk PRD doc-test.
- Tidak ada test yang di-skip, `continue-on-error`, atau penurunan cakupan. Izin workflow dibatasi ke `contents: read`; action pihak ketiga dipin ke commit SHA; tidak ada secret yang diberikan ke job.
- Build/test artifacts hanya ada di workspace runner sementara; tidak ditambahkan langkah unggah artifact.

## Validasi

- Baseline `b756b987a67c7ea449d76f9eedc4e2f6ce9a60a9`: konfigurasi/build Release bersih dan `bash claw/tests/run_all_tests.sh` lulus di Linux, pada worktree sementara. Toolchain: Clang 18.1.3, CMake 3.28.3, Ninja 1.11.1.
- Sintaks workflow diperiksa dengan `actionlint` v1.7.12.
- Job Windows belum dijalankan pada komputer ini. Verifikasi Windows harus berasal dari eksekusi GitHub Actions sesudah definisi workflow tersedia di branch default; sebelum itu tidak ada check Windows yang dapat diklaim lulus atau dijadikan required check.

## Batas rilis

Perubahan ini hanya menambah bukti CI; tidak mengubah fitur bahasa/compiler, nomor versi, tag, branch protection, atau menerbitkan release. Ini **bukan** klaim production-ready maupun rilis v1.0. Gerbang PRD berikutnya tetap mencakup borrow checking dataflow v0.7, benchmark/optimasi v0.8 (selisih maksimum 10% terhadap C `clang -O3`), dan fuzzing/pengerasan serta audit Arena v0.9 sebelum gerbang v1.0.
