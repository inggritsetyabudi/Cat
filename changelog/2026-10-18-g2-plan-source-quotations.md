# G2 — koreksi mekanis kutipan sumber PLAN

## Hasil utama

Melanjutkan PR #27 pada `docs/g2-feature-status-20261018` dari `5cc3de208652dfe4558b988f60c062ddc9ba36dd`. Perubahan ini hanya memperbaiki provenans dan kutipan persyaratan PLAN pada ledger, source index independen, dan manifest, ditambah penyesuaian validator terbatas serta regresinya yang disetujui secara eksplisit. **G2 tetap NOT CLOSED.** Kelengkapan implementasi dan pemetaan penerimaan milestone bukan hasil langkah mekanis ini.

Ketiga inventaris tetap memiliki **299 ID unik yang sama**, dengan **54 diterima compiler**, **245 diputuskan**, dan **0 diusulkan**. Tepat **19 baris ledger dan 19 entry source index** diperbarui; **280 baris/entry lainnya identik** dengan baseline. Seluruh label, judul irisan, lingkup implementasi, residual, dimensi non-compiler, fixture dan assertion manifest dipertahankan. Manifest hanya mengubah anchor normatif pada dua ID implicit-return.

## Baris yang diperbarui dan sumber persisnya

S berarti nomor baris sumber, bukan nomor bagian. Pemetaan berikut memakai [PRD-CHANGES](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md) yang tidak diubah.

| ID | Baris sumber | Koreksi |
| --- | --- | --- |
| `G2-PLAN-G2` | S34 | Satu label status per fitur; S29 tetap rujukan konteks ledger dan gerbang G2. |
| `G2-PLAN-G3` | S35 | Penggantian `Tipe(field: ...)` dengan `Tipe.new(...)`, beserta pengecualian literal di modul pendefinisi. |
| `G2-PLAN-G4` | S36 | Kapital untuk nama tipe dan `Ok/Fail/Some/None`; varian pengguna huruf kecil. |
| `G2-PLAN-G5` | S37 | `Maybe[T]`, `Some`/`None`, dan pemeriksaan sisa nama `Option`. |
| `G2-PLAN-G6` | S38 | Contoh `main` Int32 kanonis dengan `return 0`, serta varian `fn main() {}` yang sah. |
| `G2-PLAN-G7` | S39 | UTF-8 dan pembuatan dokumen ringkas otomatis dari `Ringkas:`. |
| `G2-PLAN-G8` | S40 | Larangan `modules.cat`, `pub`, dan `realm` pada contoh. |
| `G2-PLAN-M01` | S96 | Milestone v0.1 dan kolom bukti selesainya. |
| `G2-PLAN-M02` | S97 | Milestone v0.2 dan kolom bukti selesainya. |
| `G2-PLAN-M03` | S98 | Milestone v0.3 dan kolom bukti selesainya. |
| `G2-PLAN-M04` | S99 | Milestone v0.4 dan kolom bukti selesainya. |
| `G2-PLAN-M05` | S100 | Milestone v0.5 dan kolom bukti selesainya. |
| `G2-PLAN-M06` | S101 | Milestone v0.6 dan kolom bukti selesainya. |
| `G2-PLAN-M07` | S102 | Milestone v0.7 dan kolom bukti selesainya. |
| `G2-PLAN-M08` | S103 | Kutipan source index sudah benar dan dipertahankan; deskripsi dan kutipan ledger kini eksplisit. |
| `G2-PLAN-M09` | S104 | Milestone v0.9 dan kolom bukti selesainya. |
| `G2-PLAN-M10` | S105 | Gerbang v1.0 dan rujukan daftar periksanya. |
| `G2-PLAN-M02-IMPLICIT-TAIL` | S76, §3 | Kutipan «Hanya ekspresi terakhir blok»; [PRD §3 S184](../cat-northstar/PRD.md#3-if-dan-pick-sebagai-expression) tetap konteks aturan ekor blok. |
| `G2-PLAN-M02-IMPLICIT-WARNING` | S76, §3 | Kutipan «ekspresi di tengah blok → peringatan». |

G2–G8 tidak lagi memakai kutipan G2/S29 yang sama. M01–M10 kini memiliki sepuluh kutipan berbeda yang cocok persis dengan sepuluh baris milestone masing-masing; kutipan M08 tidak direkayasa sebagai kesalahan baseline. Isi dan bukti selesai dari tabel sumber disalin tanpa menafsirkan kriteria penerimaan baru. Kedua ID implicit-return mempertahankan `PLAN:M02` dan tautan S97 sebagai **staging**, bukan sumber aturan tail-only atau warning. Label tail tetap diterima hanya pada fixture calculate/main, branch if, dan penolakan fungsi bertail deklarasi yang sudah dicatat; posisi blok lain tidak dikreditkan. Label warning tetap diputuskan dan batas helper penerimaannya tidak diubah.

## Keterbatasan validator dan perbaikan yang disetujui

Validasi awal menolak anchor §3 karena aturan umum sebelumnya mengharuskan setiap `PLAN:M02` menunjuk §4. Setelah persetujuan eksplisit, `_source_anchor_ok` kini menerima sumber normatif §3 **hanya** untuk `G2-PLAN-M02-IMPLICIT-TAIL` dan `G2-PLAN-M02-IMPLICIT-WARNING`. Keduanya tetap wajib memakai `PLAN:M02`, path/anchor §3 yang tepat, serta seluruh baris normatif S76 yang cocok persis di dalam bagian itu. Anchor §4 lama bukan lagi sumber normatif yang sah bagi kedua ID tersebut. Tidak ada pengecualian bagi milestone M02 sendiri, ID mirip, atau ID PLAN lainnya. Nomor baris S76 tidak di-hardcode sebagai offset agar penyisipan baris yang tidak mengubah aturan tidak membatalkan provenans.

Enam metode regresi E2E baru memeriksa kedua ID yang sah, ID lain/serupa yang tidak mendapat pengecualian, staging yang salah, anchor milestone lama, mutasi aturan tail/warning/nomor baris tabel, dan baris aturan yang dipindah keluar §3. Semua pemeriksaan bukti compiler dan label tetap berlaku. Khusus kutipan label pada G2, ledger memakai entitas HTML `&#91;` di luar code span agar daftar label tampil benar tanpa dihitung sebagai label status kedua oleh validator existing; source index menyimpan kutipan Markdown sumber verbatim.

## Validasi dan batas bukti

- `python3 claw/tests/docs/validate_feature_status.py --root .`: **PASS** untuk ledger/source index/manifest.
- `python3 -m unittest discover -s claw/tests/docs -p 'test_feature_status*.py' -v`: **47 tes lulus** (41 existing + 6 regresi baru, dengan subkasus positif/negatif).
- `CLAW_EXE=/workspace/cat-g2-2429010-final-qeN99n/build/claw bash claw/tests/docs/run_doc_tests.sh`: validator dan **47 tes lulus**; **17 fence** diklasifikasi, **2 executable** diperiksa (**1 pass, 1 expected rejection**), **13 proposal** dan **2 text** dilewati, **0 tanpa label**. Skip bukan bukti implementasi bahasa.
- Review kutipan membandingkan tujuh row G2–G8 dengan S34–40, sepuluh row M01–M10 dengan S96–105, serta dua kutipan implicit-return dengan S76. Source index menyimpan baris tabel asli untuk global/milestone; ledger mengutip isi dan bukti selesai tanpa delimiter tabel.
- Pemeriksaan parity terhadap baseline membuktikan kesamaan seluruh 299 label, judul irisan, scope implementasi, residual, dimensi non-compiler, fixture dan assertion; 280 baris/index di luar lingkup identik. Tidak ada perubahan bukti eksekusi G3–G8 maupun M01–M10.
- Binary Linux Release existing dipakai ulang setelah identitas byte **45 file input sumber/build** diperiksa terhadap snapshot build. SHA-256 binary: `583b1b0fe4afa198e619687be33ec297b64920ab320c041b055062d31b4fe64e`. Ini pengujian dokumentasi lokal, bukan klaim full Linux/Windows CI pada head final.
- `git diff --check`: **PASS**.
- Review independen baca-saja atas kutipan, 19/280 entry, anchor manifest, batas pengecualian validator dan kesamaan bukti eksekusi: **PASS**, tanpa temuan yang memerlukan perbaikan. Reviewer juga menjalankan 26 tes validator inti dan pemeriksaan repo; seluruh 47 regresi serta doc-test dijalankan terpisah oleh pelaksana utama dan lulus pada keadaan final.

Compiler, runtime, fixture/runner eksekusi bahasa, PRD dan semantiknya tidak diubah. PR #22–#26 tidak disentuh. Tidak ada merge, version, tag, atau release. PR #27 tetap terbuka; audit kelengkapan/pemetaan penerimaan dan gerbang CI tetap pekerjaan terpisah. **G2 NOT CLOSED.**
