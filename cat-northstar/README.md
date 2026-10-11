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
