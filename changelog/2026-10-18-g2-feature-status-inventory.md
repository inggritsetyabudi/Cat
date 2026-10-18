# G2 — inventaris status fitur dan validasi dokumentasi

## Perubahan

Cabang dokumentasi tersendiri dimulai dari main `e7789c6efedff677b81c2f883fe8498e32c30554`. Audit sebelum edit mengonfirmasi PR #22–#26 masih terbuka; implementasi dan bukti pada cabang-cabang itu tidak dikreditkan ke main, dan cabang-cabangnya tidak diubah.

[FEATURE-STATUS.md](../cat-northstar/FEATURE-STATUS.md) menjadi rumah kanonik label G2 per irisan yang dapat diuji mandiri. Kolom keputusan semantik, lingkup implementasi, bukti penerimaan, sisa, dan dimensi non-compiler dipisahkan. PRD dan rencana perubahan menautkannya; aturan agen diselaraskan dengan persetujuan Setya. Simbol keputusan historis, metadata parsial, kategori fence, serta versi target dipertahankan maknanya dan tidak menjadi label G2 kedua.

Kewenangan reasoning, brainstorming, dan pengajuan desain proaktif dicatat. Keputusan yang diratifikasi tetap otoritatif; usulan tidak menjadi keputusan tanpa ratifikasi, dan perubahan semantik material tidak diimplementasikan dalam pekerjaan ini.

Audit juga menemukan bahwa STATUS, README, dan SPEC masih menyatakan PR #21 belum masuk main. Ketiganya diselaraskan dengan fondasi Str/String yang sudah ada pada baseline; tidak ada perluasan kontrak atau implementasi bahasa.

## Pemeriksaan

Validator deterministik dan tes negatif unit/end-to-end ditambahkan ke runner doc-test dan otomatis diwarisi suite penuh Linux/Windows UCRT64. Validator memeriksa struktur, label tunggal, ID unik, cakupan heading sumber aktif, referensi bukti lokal, dan kontrak lingkup terbatas. Ia tidak dapat membuktikan kecukupan semantik fixture; review independen tetap diperlukan.

Hasil pengujian lokal dan CI pada head final dicatat pada PR review. Daftar file yang berubah tidak mencakup compiler atau runtime. Tidak ada merge, perubahan versi, tag, atau release.

## Status

**G2 belum ditutup.** Inventaris ini bukan bukti seluruh milestone v0.1–v0.3 selesai. Pertanyaan desain yang sungguh belum diputuskan dan sisa penerimaan tercantum pada ledger; pekerjaan compiler yang telah diputuskan tetapi belum diterima tidak dibuka kembali sebagai usulan. Penerimaan suatu irisan tidak berarti bebas bug atau layak produksi. Matriks PR #25 hanya referensi review yang dipin, bukan dokumen atau hasil penerimaan pada main.

## Hasil lokal sebelum PR

Clean configure/build Release Clang berhasil. Suite lengkap lokal berhasil dengan compiler hasil build tersebut: validator ledger, 12 tes unit/end-to-end validator, doc-test PRD (17 fence: 2 executable, 13 proposal dilewati, 2 text dilewati), CLI dokumentasi, frontend, OIR, backend LLVM, native, dan CLI status. Review independen menemukan ketidakcocokan beberapa klaim dengan bukti; rujukan skalar, try, view/slicing, locality implements, dan cakupan init telah dipersempit atau diperbaiki. Regresi unittest discovery juga diperbaiki; tidak memakai loop yang menyamarkan jumlah tes.

Inventaris final memuat 92 ID unik: 35 irisan diterima compiler, 55 keputusan tanpa penerimaan compiler pada lingkupnya, dan 2 proposal edge pembagian integer. Jumlah tersebut tidak berarti audit atomisasi seluruh kalimat PRD selesai. Review independen menyarankan penerbitan sebagai inventaris G2 parsial setelah koreksi dan validasi, bukan penutupan G2. CI head PR tetap harus diamati, bukan diasumsikan dari hasil lokal.
