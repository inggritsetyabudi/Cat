# G2: staging rewrite v1.x berbeda dari usulan aturan macro

`G2-PRD-15-REWRITE` tetap **[diputuskan]**, khusus keberadaan/staging rewrite di v1.x, bukan v1. [PRD §15](../cat-northstar/PRD.md#15-foreign-c-annotation-dan-rewrite) menyatakan keputusan itu; [header ROADMAP](../cat-northstar/ROADMAP-v1-plus.md#roadmap-c-setelah-v1) secara eksplisit mengecualikannya dari status usulan umum. Kutipan indeks stage sekarang memuat seluruh pernyataan PRD §15 yang sebenarnya, bukan menyimpulkan keputusan dari rencana migrasi.

S80 di [rencana perubahan §3](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#3-perubahan-per-bagian-prd) tetap verbatim: label usulan, pindah roadmap v1+, tanpa contoh `cat`. Itu provenance migrasi historis. Tidak bertentangan dengan ratifikasi staging Setya 11 Oktober: aturan rinci pada ROADMAP §4 dan target spesifik v1.3 tetap usulan. Tidak perlu meminta ulang keputusan staging atau mengubah PRD.

## Satu owner untuk setiap aturan yang masih usulan

Enam bullet **Aturan usulan** dan tiga penutup risiko di [ROADMAP §4](../cat-northstar/ROADMAP-v1-plus.md#4-v13-rewrite-macro) dipecah menjadi 16 atom mandiri. Seluruh ID berikut berawalan `G2-ROADMAP-4-REWRITE-`, bersumber `ROADMAP:4`, dan berlabel **[diusulkan]**:

| Sufiks ID | Aturan yang dimiliki | Sumber |
| --- | --- | --- |
| EXPANSION-ORDER | Ekspansi sebelum pengecekan tipe | L82 |
| EXPANSION-AST | Hasil ekspansi adalah AST biasa | L82 |
| TYPE-CHECK | Hasil menjalani pengecekan tipe seperti kode tulisan tangan | L82 |
| SAFE-CHECK | Hasil macro tidak memiliki jalan pintas Safe | L82 |
| HYGIENE | Nama buatan macro tidak bentrok dengan nama call-site | L83 |
| NO-IO | Tidak ada I/O saat ekspansi | L84 |
| NO-EXTERNAL | Tidak ada pemanggilan kode luar saat ekspansi | L84 |
| DEPTH-LIMIT | Batas kedalaman ekspansi | L85 |
| NODE-LIMIT | Batas jumlah node ekspansi, bukan lint | L85 |
| CALLSITE-DIAGNOSTIC | Lokasi error menunjuk call-site | L86 |
| EXPANSION-DIAGNOSTIC | Pesan error menyebut ekspansi | L86 |
| USER-ANNOTATION-ENGINE | Annotation pengguna memakai mesin macro yang sama | L87 |
| EXPAND-TOOL | `claw expand` menampilkan hasil ekspansi | L93 |
| SIZE-LINT | Lint untuk macro yang menghasilkan lebih dari N node | L93 |
| SOURCE-SPAN | Setiap node hasil membawa lokasi asal | L94 |
| AST-ONLY-ENGINE | Ekspander hanya memanipulasi AST, tanpa logika sembarang | L95 |

Bentuk AST hasil berbeda dari batas kemampuan mesin. Pemetaan lokasi pada node berbeda dari penyajian lokasi error dan konteks ekspansinya. Batas node keras berbeda dari lint ukuran. Tidak ditetapkan angka baru untuk batas kedalaman/node atau N lint. Tujuan keterbacaan dan contoh sintaks tidak dijadikan aturan baru yang diratifikasi. Annotation bawaan v1 tetap pada owner PRD:15; annotation pengguna bukan penerimaan annotation bawaan. Gerbang masa depan generik `G2-PLAN-M10` tidak diduplikasi.

## Inventaris dan validator

Ledger, manifest atom, dan source index bertambah dari 347 menjadi **363 ID**: **54 [diterima compiler]**, **293 [diputuskan]**, **16 [diusulkan]**. Label ID lama tidak berubah. Atom usulan tidak memiliki fixture/assertion penerimaan compiler; kutipan sumber bukan bukti implementasi.

Validator menerima `ROADMAP:<section>` hanya dengan file sumber `cat-northstar/ROADMAP-v1-plus.md` dan nomor heading yang cocok. Ini validasi sumber yang ditunjuk, bukan kewajiban menginventaris seluruh roadmap. Jalur PRD/PLAN lama dipertahankan; pengecualian sumber implicit-return tidak diperluas. Sumber ROADMAP khusus proposal: kenaikan label menjadi keputusan atau acceptance ditolak; staging yang diratifikasi tetap memakai owner PRD. Regresi mencakup sumber sah, file/nomor/heading yang salah, ketidakcocokan index/manifest, dan eskalasi label.

Pengujian sumber terfokus `test_feature_status_rewrite.py` mengunci 16 owner dan kutipan verbatim, staging PRD, header roadmap, serta konteks historis S80. Jalankan:

```bash
python3 claw/tests/docs/validate_feature_status.py --root "$PWD"
python3 -m unittest discover -s claw/tests/docs -p 'test_feature_status*.py' -v
python3 -m unittest discover -s claw/tests/docs -p 'test_feature_status_rewrite.py' -v
bash claw/tests/docs/run_doc_tests.sh
```

Hasil eksekusi dan review independen serta CI Linux/Windows harus dicatat pada PR untuk head yang diuji, bukan diprediksi dari provenance. **G2 tetap NOT CLOSED** menunggu audit bukti/cakupan penuh yang terpisah. Perubahan ini hanya dokumentasi/inventaris/validator dan regresinya: tidak mengubah compiler, Arena, f-string/Show, PR lain, versi, tag, rilis, atau melakukan merge.
