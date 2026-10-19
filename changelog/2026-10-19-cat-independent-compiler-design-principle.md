# Prinsip desain compiler Cat yang mandiri

## Keputusan

Keputusan Setya pada 19 Oktober 2026 dicatat sebagai prinsip normatif di `cat-northstar/PRD.md`, dengan penerapan review di `PRD-CHANGES-v0.1-to-v1.0.md` dan keberlanjutan di `ROADMAP-v1-plus.md`. Prinsip berlaku mulai sekarang, termasuk sebelum v0.5, hingga v1 dan sesudahnya agar revisi desain tidak menumpuk ketika Cat digunakan luas.

Cat boleh mempelajari konsep compiler umum, tetapi teknologinya wajib memiliki semantik, nama, spesifikasi, dan implementasi mandiri sesuai kebutuhan Cat. Copy-paste atau sekadar mengganti nama internal Rust dilarang; istilah compiler umum tetap boleh digunakan. Usulan teknologi berikutnya harus menjelaskan kebutuhan, kontrak/invariant, penamaan, sumber konsep, alternatif/trade-off, dan rencana implementasi serta tes mandiri.

Menjadi lebih baik daripada Rust adalah tujuan pada aspek tertentu, bukan capaian yang diklaim oleh perubahan ini. Klaim memerlukan kriteria penerimaan eksplisit dan tes/benchmark yang dapat direproduksi, dengan baseline/versi, konfigurasi/toolchain, workload, metrik, serta hasil aktual.

## Batas perubahan

- Hanya dokumentasi; tidak menentukan atau meratifikasi pipeline, urutan pass, atau nama tahap baru.
- Tidak mengubah keputusan semantik yang telah diratifikasi, staging versi, maupun status implementasi fitur/G2. Perubahan material tetap memerlukan ratifikasi Setya tersendiri.
- Tidak mengaudit keseluruhan implementasi lama, menandai milestone selesai, atau mengklaim Cat sudah lebih unggul daripada Rust.
- Dokumen historis, termasuk `revise/`, tidak diubah dan tidak dijadikan otoritas.

## Validasi

- `git diff --check`: lulus.
- `python3 tools/generate_prd_summary.py --check`: ringkasan tetap sinkron; tidak ada perubahan baris `Ringkas:` maupun contoh program.
- `python3 claw/tests/docs/test_prd_summary.py`: 14 tes lulus.
- `python3 claw/tests/docs/validate_feature_status.py`: lulus setelah penempatan catatan secara aditif di akhir dokumen. Sisipan awal di tengah dokumen menggeser nomor baris bukti; penempatan akhir menjaga rujukan sumber lama tanpa mengubah ledger atau validator.
- Review independen read-only memeriksa batas ratifikasi, nomenklatur umum, bukti klaim, dan staging; guardrail tersebut tercakup dalam teks.

Suite compiler, benchmark, dan CI lintas platform tidak dijalankan untuk perubahan dokumentasi ini; tidak ada hasil implementasi atau klaim performa baru.
