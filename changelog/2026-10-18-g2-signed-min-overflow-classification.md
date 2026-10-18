# G2 — koreksi klasifikasi overflow signed MIN/-1

## Koreksi

`cat-northstar/FEATURE-STATUS.md` mengoreksi `G2-PRD-6-DIV-MIN` dari proposal menjadi keputusan yang sudah diratifikasi, tanpa mengklaim penerimaan compiler. PRD §6/§19 menetapkan overflow signed sebagai defect pada debug dan wrapping pada release; rencana perubahan §3 baris 19/23 dan §4 v0.8 mempertahankan keputusan dan target implementasi tersebut. Tidak ada pengecualian khusus MIN/-1.

Profil dicatat secara persis: `MIN / -1` dan `MIN % -1` menghasilkan defect pada debug; release menghasilkan quotient MIN dan remainder 0. Lowering baseline `claw/src/codegen/llvm_emit.cpp` baris 631–638 langsung menggunakan `sdiv`/`srem`, sehingga tetap dicatat sebagai gap implementasi/penerimaan, bukan bukti kepatuhan profil. PR #23 masih terbuka dan tidak dikreditkan ke main.

Daftar keputusan owner tidak lagi membuka ulang overflow umum. Hanya pembagi nol `/` dan `%` tetap berupa proposal belum diratifikasi, dengan usulan defect runtime dan alternatif penolakan kompilasi konstanta dipisahkan. Rekap 92 ID menjadi 35 diterima compiler, 56 diputuskan, dan 1 diusulkan.

## Batas

Ini koreksi satu dokumen pada workstream PR #27 dari baseline main `e7789c6efedff677b81c2f883fe8498e32c30554`; changelog inventaris sebelumnya tetap merupakan catatan historis sebelum koreksi. Tidak ada perubahan compiler, runtime, semantik, Arena, atau f-string; tidak ada perubahan/rebase PR #22–#26, merge, versi, tag, atau release. G2 tetap belum ditutup.

## Validasi

Validator ledger terfokus dan 12 tes unit/end-to-end lulus. Salinan worktree terpisah mempertahankan artefak lama tanpa perubahan; CMake/Ninja dengan Clang dikonfigurasi ke direktori build Release baru, lalu build, doc-test, dan `claw/tests/run_all_tests.sh` lulus dengan compiler hasil build tersebut. Doc-test mencakup 17 fence PRD (2 executable, 13 proposal dan 2 text dilewati); suite penuh mencakup CLI dokumentasi, frontend, OIR, backend LLVM, native, dan CLI status.

Review independen atas koreksi memberi APPROVE tanpa temuan blocking/nonblocking, termasuk pemeriksaan sumber kanonis, gap lowering, batas PR23, dan hitungan 35/56/1. Hasil CI Linux/Windows UCRT64 harus diamati pada head koreksi yang sama dan dicatat pada PR #27 sebelum koreksi disebut tervalidasi. Entri ini tidak menganggap hasil baseline atau PR lain sebagai hasil koreksi.
