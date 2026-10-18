# G2 — acceptance milestone dan gerbang rilis yang dapat diverifikasi

Perubahan dokumentasi ini menambahkan **29 ID PLAN** dengan label **[diputuskan]** ke ledger, independent source index, dan evidence manifest secara sinkron. Total menjadi **347 ID: 54 diterima compiler, 293 diputuskan, 0 diusulkan**. Semua 318 ID lama dan seluruh record evidence lama dipertahankan. **G2 NOT CLOSED.**

## Owner baru

Nomor S menunjuk baris sumber [PRD-CHANGES](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md). Setiap excerpt index berasal dari baris sumber itu; bukan diturunkan dari judul ledger. ID milestone lama M01–M10 tetap identitas/staging, bukan owner acceptance kedua.

| Sumber | ID baru | Kewajiban |
| --- | --- | --- |
| S96 | `G2-PLAN-M01-D-CHECKLIST` | M01: checklist seluruh perubahan D |
| S97 | `G2-PLAN-M02-PRECEDENCE-TABLE` | M02: acceptance tabel presedensi |
| S97 | `G2-PLAN-M02-BRANCH-IF` | M02: acceptance cabang if |
| S97 | `G2-PLAN-M02-BRANCH-PICK` | M02: acceptance cabang pick |
| S97 | `G2-PLAN-M02-BRANCH-NEVER` | M02: acceptance cabang Never |
| S97 | `G2-PLAN-M02-BRANCH-TRY` | M02: acceptance cabang try |
| S97 | `G2-PLAN-M02-BRANCH-IMPLICIT` | M02: acceptance cabang implicit return |
| S98 | `G2-PLAN-M03-EXHAUST-PROPERTY` | M03: property-based exhaustiveness |
| S99 | `G2-PLAN-M04-LEXER-RADIX` | M04: matriks lexer Radix |
| S99 | `G2-PLAN-M04-LEXER-SEPARATOR` | M04: matriks lexer `_` |
| S99 | `G2-PLAN-M04-LEXER-SUFFIX` | M04: matriks lexer suffix |
| S99 | `G2-PLAN-M04-LEXER-CHAR` | M04: matriks lexer `Char` |
| S99 | `G2-PLAN-M04-LEXER-ESCAPE` | M04: matriks lexer escape |
| S99 | `G2-PLAN-M04-LEXER-FSTRING` | M04: matriks lexer f-string |
| S99 | `G2-PLAN-M04-LEXER-DOC` | M04: matriks lexer `///` |
| S100 | `G2-PLAN-M05-CLOSURE-MAP` | M05: map dengan closure berjalan |
| S100 | `G2-PLAN-M05-CLOSURE-FILTER` | M05: filter dengan closure berjalan |
| S101 | `G2-PLAN-M06-FFI-NEGATIVE` | M06: suite negatif FFI |
| S102 | `G2-PLAN-M07-VALID-RECOVERY` | M07: program valid yang dulu ditolak lolos |
| S103 | `G2-PLAN-M08-CORE-BENCHMARK` | M08: eksekusi benchmark inti |
| S103 | `G2-PLAN-M08-IR-C-COMPARE` | M08: perbandingan IR dengan C setara |
| S104 | `G2-PLAN-M09-PARSER-FUZZ` | M09: kampanye fuzzing parser |
| S104 | `G2-PLAN-M09-ROBUSTNESS` | M09: uji ketahanan |
| S104 | `G2-PLAN-M09-RANDOM-NOCRASH` | M09: tidak crash pada corpus input acak |
| S105 | `G2-PLAN-M10-RELEASE-CHECKLIST` | M10: checklist rilis lengkap |
| S115 | `G2-PLAN-M10-SELFHOST-FILE-IO` | M10: kecukupan file I/O untuk self-hosting |
| S115 | `G2-PLAN-M10-SELFHOST-STRING-BUILDER` | M10: kecukupan string builder untuk self-hosting |
| S115 | `G2-PLAN-M10-SELFHOST-VEC` | M10: kecukupan vec untuk self-hosting |
| S115 | `G2-PLAN-M10-SELFHOST-MAP` | M10: kecukupan map untuk self-hosting |

## Bukti tersedia, bukan acceptance penuh

Doc-test sudah terpasang: `run_doc_tests.sh` dipanggil `run_all_tests.sh`, dan CI Linux/Windows memanggil suite penuh. Runner saat ini memeriksa satu fence positif dan satu negatif dengan diagnostik; 13 proposal dan dua text dilewati secara eksplisit. Ini tidak membuktikan seluruh perubahan D, semua contoh PRD atau fitur compiler selesai.

Runner frontend memeriksa AIR presedensi operator A–B (arithmetic, subtraction, logical, not, comparison) serta beberapa penolakan operator. Fixture if/pick, try, implicit return, literal radix/separator/suffix, Char dan escape mempunyai bukti parsial pada owner/runner lama. Fixture tuple/range bukan property-based exhaustiveness. LLVM IR yang dihasilkan fixture bukan perbandingan IR dengan C setara. Tidak ada promosi acceptance compiler baru.

Manifest 29 owner baru sengaja memakai `fixtures: []` dan `assertions: []`: belum ada bukti **gate penuh** yang dikreditkan. Ledger membedakan bukti parsial yang tersedia dari matriks/corpus/hasil lengkap yang belum dikreditkan. “Belum dikreditkan” bukan klaim telah mencari seluruh riwayat atau bahwa artefak tidak pernah ada.

## Owner lama yang dirujuk, tidak diduplikasi

M01 doc-test positif/negatif tetap `G2-PRD-0-DOC` / `G2-PRD-0-ERROR`; instalasi/kategori runner tetap `G2-PLAN-G1`. Modularitas dan konstruksi tetap atom PRD:17 dan PRD:1. M05 eliminasi boilerplate new dirujuk ke `G2-PRD-15-ANNOTATION-CONSTRUCTOR`; gerbang umum tetap `G2-PRD-18-NO-BOILERPLATE`. M07 retensi suite negatif tetap `G2-PRD-9-SAFE-SUITE`; keanggotaan private leak v1 tetap `G2-PLAN-SAFE-PRIVATE-SUITE`. Audit Arena tetap `G2-PRD-19-ARENA`, bukan owner baru.

M10 mengagregasi semua kewajiban S111–115 secara eksplisit pada crosswalk ledger: Safe, private leak, Simple, boilerplate, doc-tests, ambang Fast ≤10% dari C clang -O3, release overflow, noalias, hidden runtime cost, stabilitas edition, dan empat kecukupan pustaka. Semantik wrap/noalias/bounds, closure, literal dan FFI tetap di ID PRD lama. Empat owner kecukupan pustaka adalah acceptance workload, bukan API bahasa baru. `G2-PRD-18-SELFHOST-PREREQS` dipersempit menjadi agregasi yang merujuk empat owner itu; excerpt/anchor dan labelnya tetap. Ini satu-satunya record source-index lama yang deskripsinya diperjelas; manifest lama tidak berubah.

## Bukti yang masih kurang

Checklist perubahan D; matriks presedensi dan cabang if/pick/Never/try/implicit return lengkap; properti exhaustiveness; tujuh keluarga matriks lexer; generator new dan operasi map/filter closure; suite FFI negatif; retensi suite Safe serta pasangan program valid sebelum/sesudah checker dataflow; benchmark inti dan IR C setara; kampanye fuzzing, ketahanan, hasil input acak tanpa crash dan audit Arena; sign-off checklist rilis, kompatibilitas edition dan kecukupan file I/O/string builder/vec/map. Owner normatif yang sudah ada tidak dianggap bukti pemenuhan gate.

## Batas perubahan dan discrepancy

Tidak ada implementasi tes baru, benchmark, fuzzing, library, compiler, atau perubahan semantik PRD. Tidak ada perubahan Arena/f-string, merge, versi, tag, atau rilis. PR terbuka lain dipertahankan. S80 tetap berbunyi «Label `[diusulkan]`, dipindah ke dokumen roadmap v1+», sedangkan row `G2-PRD-15-REWRITE` tetap `[diputuskan]`; keduanya **tidak diubah**, dan discrepancy tetap ditandai untuk rekonsiliasi keputusan yang diratifikasi.

## Validasi

Pada salinan disposable dengan compiler Linux Release dari baseline compiler yang tidak berubah:

- Validator ledger/source index/manifest: **PASS**, 347 ID konsisten.
- Regression `test_feature_status*.py`: **53 tests, OK**.
- Doc-tests: **17 fence**, 2 executable (1 passed, 1 expected rejection), 13 proposal dan 2 text dilewati eksplisit, 0 untagged. Tidak ada acceptance untuk fence yang dilewati.
- `bash claw/tests/run_all_tests.sh`: **PASS**, termasuk docs, frontend, OIR, backend LLVM, native dan CLI.
- `git diff --check`: **PASS**. Seluruh 318 ID lama dipertahankan; 318 record manifest lama identik; S80/rewrite, Arena dan f-string tidak berubah.
- Review independen baca-saja memverifikasi cakupan S96–115, excerpt verbatim, satu owner per kewajiban, label dan kejujuran evidence. Temuan overlap provenance M02 diperbaiki: owner cabang kini mengutip hanya `tes cabang`, bukan frasa tabel presedensi. Kutipan M05 dipersempit menjadi `map` dan `filter` masing-masing tanpa mengubah semantik.

Snapshot sesudah koreksi provenance diperiksa kembali sebelum commit. CI Linux/Windows diperiksa pada head PR baru yang sama dan hasilnya dilaporkan di handoff; pemeriksaan lokal tidak menutup G2. Audit cakupan seluruh PRD dan kecukupan evidence tetap perlu disahkan secara independen; sisa implementasi fitur tidak otomatis menghalangi penutupan inventaris G2.
