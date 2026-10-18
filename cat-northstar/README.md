# Cat Northstar — Acuan Utama Bahasa C@

**Status:** sumber acuan desain yang disetujui Setya, 11 Oktober 2026.

Folder ini menggantikan `revise/` sebagai rumah dokumen utama pembuatan bahasa C@. Ketiga dokumen keputusan yang diberikan dipertahankan di sini; isi sumber disalin tanpa mengubah keputusan atau contoh di dalamnya.

## Dokumen utama

1. [`PRD.md`](./PRD.md) — paket keputusan desain bahasa; acuan untuk sintaks, semantik, dan prinsip desain.
2. [`PRD-CHANGES-v0.1-to-v1.0.md`](./PRD-CHANGES-v0.1-to-v1.0.md) — daftar perubahan PRD, versi compiler, milestone v0.1–v1.0, dan bukti penerimaan.
3. [`ROADMAP-v1-plus.md`](./ROADMAP-v1-plus.md) — arah setelah v1, termasuk fitur yang sengaja ditunda dan gerbang sebelum dikerjakan.

**Ratifikasi:** pada 11 Oktober 2026, Setya secara eksplisit meratifikasi seluruh keputusan pada tabel PRD, termasuk entri yang semula bertanda 🟦. Tanda itu hanya jejak status saat dokumen disusun, bukan isu terbuka. Ratifikasi tidak mengubah batas versi dan staging pada roadmap.

Gunakan ketiganya bersama-sama: PRD menetapkan rancangan, rencana perubahan mengatur pekerjaan sampai v1, dan roadmap menetapkan batas serta urutan pasca-v1. Pertahankan penanda status dan batas versi sebagaimana tertulis; item yang ditempatkan setelah v1 bukan persyaratan implementasi v1.

## Urutan kewenangan

- Dokumen di folder ini mencerminkan keputusan desain terbaru dan mengungguli rancangan lama yang bertentangan.
- [`legacy/`](./legacy/) menyimpan dua PRD lama untuk riwayat dan perbandingan saja; isinya bukan sumber keputusan terbaru.
- Folder `ideas/` di root repository hanya arsip latar belakang. Beberapa berkasnya memakai sintaks dan keputusan lama yang bertentangan, sehingga jangan gunakan sebagai spesifikasi aktif.
- Persetujuan desain tidak otomatis berarti fitur sudah tersedia di compiler. Status implementasi harus dibuktikan dari kode, suite tes, dan [`changelog/`](../changelog); jangan menganggap label `[sudah]` pada PRD lama sebagai bukti implementasi saat ini.

## Sumber keputusan

`PRD.md` berasal dari `cat-paket-solusi.md`, rencana perubahan dari `cat-perubahan-prd-v0.md`, dan roadmap dari `cat-roadmap-v1-plus.md`. Nama dokumen di sini menegaskan perannya sebagai acuan proyek.

## Ringkasan otomatis (G7)

[`PRD-RINGKAS.md`](./PRD-RINGKAS.md) adalah keluaran turunan, bukan PRD otoritatif kedua. Sumber ringkasannya hanya baris yang ditulis manusia dalam [`PRD.md`](./PRD.md): tepat satu `Ringkas: <teks tidak kosong>` pada baris nonkosong pertama setelah **setiap heading tingkat dua (`##`) di luar fence kode**, termasuk tabel Ringkasan dan bagian bernomor 0–20. Judul dokumen, pengantar, dan subbagian `###` atau lebih dalam tidak membutuhkan marker sendiri. Marker harus satu baris, tanpa indentasi atau whitespace di ujung; ejaan `Ringkas: ` bersifat exact. Marker di dalam fence bukan sumber ringkasan. Marker tambahan, salah format, salah posisi, atau bagian tanpa marker menggagalkan generator.

Jalankan dari root repository (Python 3, standard library saja):

```bash
python3 tools/generate_prd_summary.py
python3 tools/generate_prd_summary.py --check
```

Commit sumber marker dan hasil regenerasi bersama-sama; jangan menyunting keluaran langsung. Generator menyalin heading dan isi marker sesuai urutan sumber, tanpa merangkum paragraf lain, mengambil teks legacy, atau mengubah status keputusan. Header/provenance keluaran tetap berasal dari template generator. Jika bagian/marker dihapus atau diubah, regenerasi membangun ulang seluruh keluaran sehingga teks lama tidak tersisa.

I/O menggunakan byte dan decode UTF-8 ketat: sumber tidak pernah ditulis ulang, tidak ada normalisasi Unicode atau perbaikan mojibake otomatis. Sumber LF maupun CRLF diterima; keluaran selalu UTF-8 tanpa BOM dengan LF dan tepat satu newline terakhir, termasuk di Windows. Ini adalah default tooling yang dapat diubah melalui review, bukan keputusan desain bahasa.

Mode `--check` hanya membaca dan membandingkan byte exact, gagal bila hasil hilang atau usang. [`claw/tests/docs/run_doc_tests.sh`](../claw/tests/docs/run_doc_tests.sh) menjalankan check ini, regresi generator, dan pemeriksaan UTF-8 ketat atas keenam dokumen sumber/arsip yang sudah ada di folder ini. Runner tersebut dipanggil suite penuh dan CI Linux serta Windows UCRT64; CI tidak memperbaiki atau meregenerasi file diam-diam.

Gate inventarisasi, klasifikasi, dan bukti G2 sudah ditutup di `main`, sebagaimana dicatat dalam [changelog penutupan G2](../changelog/2026-10-19-g2-inventory-gate-closure.md). Penutupan gate tersebut bukan klaim kelengkapan compiler atau penerimaan seluruh v0.1; penyelesaian G7 juga tidak memberikan penerimaan versi. Tidak ada perubahan pada Arena, fitur bahasa, staging v0.4+, versi, atau kebijakan rilis.
