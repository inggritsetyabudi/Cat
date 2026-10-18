# G2 — kelengkapan pemilik governance dan migrasi dokumentasi

Perubahan ini menambah **19 ID PLAN** ke ledger, source index kanonik, dan evidence manifest. Hitungan menjadi **318 ID: 54 diterima compiler, 264 diputuskan, 0 diusulkan**. Seluruh 299 baris dan bukti sebelumnya dipertahankan. Tidak ada penerimaan compiler baru, implementasi fitur, merge, tag, rilis, atau kenaikan versi. **G2 NOT CLOSED.**

## Pemilik baru

S adalah nomor baris pada [PRD-CHANGES](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md), bukan nomor bagian PRD aktif. Setiap cuplikan indeks disalin persis dari sumber, termasuk ejaan `vec` dan `fn(A) -> R`.

| Sumber | ID baru | Kewajiban mandiri |
| --- | --- | --- |
| S58 | `G2-PLAN-ARENA-EXPERIMENTAL` | Label dokumentasi eksperimental |
| S58 | `G2-PLAN-ARENA-V1-EXCLUSION` | Pengecualian dari gerbang v1 |
| S58 | `G2-PLAN-MIGRATE-NODE` | Migrasi artefak contoh Node |
| S58 | `G2-PLAN-MIGRATE-CACHE` | Migrasi artefak contoh Cache |
| S58 | `G2-PLAN-MIGRATE-DOUBLY-LINKED` | Penggantian contoh doubly linked dengan vec + indeks |
| S58 | `G2-PLAN-MIGRATE-GRAPH` | Penggantian contoh graph dengan vec + indeks |
| S82 | `G2-PLAN-CLOSURE-SECTION` | Bagian closure baru |
| S82 | `G2-PLAN-CLOSURE-SHORT-DOC` | Dokumentasi bentuk pendek |
| S82 | `G2-PLAN-CLOSURE-BLOCK-DOC` | Dokumentasi bentuk blok |
| S82 | `G2-PLAN-CLOSURE-FN-TYPE-DOC` | Dokumentasi tipe parameter fn(A) -> R |
| S82 | `G2-PLAN-CLOSURE-BORROW-NEGATIVE` | Tes penolakan mutasi variabel terpinjam |
| S111 | `G2-PLAN-SAFE-PRIVATE-SUITE` | Kategori kebocoran tipe privat masuk suite Safe wajib |
| S114 | `G2-PLAN-EDITION-STABILITY` | Perubahan breaking pasca-v1 hanya melalui edition |
| S84 | `G2-PLAN-HISTORICAL-EXAMPLE-AUDIT` | Audit casing/contoh dengan checklist enam bagian historis |
| S55 | `G2-PLAN-MIGRATE-CONFIG-SHAPE` | Artefak Config shape/choice historis §8 |
| S61 | `G2-PLAN-MIGRATE-BUFFER` | Artefak Buffer contract/implements |
| S61 | `G2-PLAN-MIGRATE-CIRCLE` | Artefak Circle contract/implements |
| S62 | `G2-PLAN-MIGRATE-VECTOR2` | Artefak Vector2 operator overloading |
| S68 | `G2-PLAN-MIGRATE-CONFIG-IDENTITY` | Artefak Config identitas historis §22 |

## Batas pemilik dan bukti

Audit safety Arena tetap `G2-PRD-19-ARENA`; milestone audit tetap M09. Capture, move, dan return closure tetap pada tiga pemilik PRD:20; M05 hanya staging. `G2-PRD-17-PRIVATE` tetap semantik penolakan kebocoran, bukan bukti keanggotaan suite wajib. Lima kategori Safe lain tetap `G2-PRD-9-SAFE-SUITE`. Aturan casing dan konstruksi global tidak diduplikasi: ID migrasi hanya artefak tertentu, di bawah G3/G4 dan pemilik semantik PRD masing-masing.

Ledger memetakan S84 §§1,2,17,29,30,32 historis ke PRD aktif dan dokumentasi toolchain. Tabel itu bukan bukti checklist audit/perbaikan sudah selesai. Contoh pada `legacy/` adalah jejak asal, bukan otoritas desain. `fixtures` dan `assertions` atom baru sengaja kosong: bukti penyelesaian artefak/governance spesifik belum dikreditkan dan dicatat sebagai residual. Menambah owner tidak sama dengan menyelesaikan rewrite atau mengesahkan bahasa closure.

Validator diperluas secara terbatas untuk `PLAN:S<n>`: nomor baris sumber, heading yang menaungi, dan cuplikan verbatim harus cocok. Sumber PLAN:S tidak boleh diberi label penerimaan compiler. Kategori evidence G3–G8, provenance UTF-8/ringkasan G7, dan evidence milestone tidak diubah.

## Discrepancy yang tidak diselesaikan

[S80](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#3-perubahan-per-bagian-prd) menyatakan persis **«Label `[diusulkan]`, dipindah ke dokumen roadmap v1+»** untuk `rewrite`. Baris `G2-PRD-15-REWRITE` saat ini berlabel **`[diputuskan]`**. S80 dan baris tersebut tidak diubah atau direklasifikasi. Coordinator perlu merekonsiliasi perbedaan ini dengan keputusan yang diratifikasi, bukan menyimpulkan ratifikasi hanya dari label historis atau staging.

## Validasi

Pada salinan disposable dengan build Linux Release dari baseline compiler yang tidak berubah:

- Validator ledger, source index, dan manifest: **PASS**, 318 ID konsisten.
- Regression suite `test_feature_status*.py`: **53 tests, OK**, termasuk sumber S positif, baris hilang/salah heading, cuplikan dari baris lain dalam heading yang sama, sumber malformed, dan larangan acceptance compiler untuk PLAN:S.
- Doc-tests: **17 fence terklasifikasi**; 2 executable diperiksa (1 lolos, 1 ditolak sesuai diagnostik), 13 proposal dan 2 text dilewati secara eksplisit; 0 tanpa label. Ini bukan acceptance untuk contoh yang dilewati.
- `bash claw/tests/run_all_tests.sh`: **PASS** (docs, frontend, OIR, backend LLVM, native, CLI); build Release dan `git diff --check` juga lolos.
- Review independen baca-saja atas diff dilakukan. Pengikatan excerpt ke baris S dikonfirmasi; temuan typo sintaks guard dan assertion stdout/stderr pada tes malformed diperbaiki. Snapshot sesudah perbaikan lulus seluruh pemeriksaan di atas.

Tidak ada cleanup pada worktree pemilik; build dan artefak suite berada di salinan testing disposable. Lulus pemeriksaan lokal tidak mengesahkan kelengkapan G2 ataupun menggantikan CI Linux/Windows pada head PR final. PR #22–#26 tidak disentuh.
