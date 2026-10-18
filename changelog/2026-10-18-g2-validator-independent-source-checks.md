# G2 validator — inventori sumber independen dan assertion stdout

## Perubahan

Menambahkan `cat-northstar/feature-status-source-index.json` sebagai crosswalk sumber tersendiri untuk 164 atom G2. ID stabil, sumber PRD/PLAN, anchor, deskripsi persyaratan, dan kutipan sumber dipelihara terpisah dari ledger status serta manifest bukti. Validator membaca crosswalk tersebut, bukan membangunnya ulang dari kedua daftar yang diperiksa. ID ledger, manifest, dan crosswalk harus sama; pemetaan sumber dan anchor diperiksa terpisah. Menghapus `G2-PRD-2-ARRAY-I32` sekaligus dari `FEATURE-STATUS.md` dan `feature-status-atoms.json` kini ditolak selama inventori sumber masih memuat atom tersebut.

Bukti native yang sebelumnya hanya menunjuk assignment `expected_*_stdout` kini menunjuk kondisi perbandingan stdout sebenarnya. Field opsional `checks` pada assertion manifest mencatat perbandingan tambahan pada jalur sanitizer tanpa bergantung pada nomor baris. Validator menolak assignment variabel sebagai assertion penerimaan. Untuk bentuk stdout yang didukung, pemeriksaan struktural mensyaratkan satu kondisi mismatch yang dapat dikenali sebagai command shell, capture runtime, dan jalur kegagalan dengan `exit` nonzero tanpa kontrol bersyarat tambahan. Komentar dan badan heredoc bukan assertion aktif; wrapper fungsi atau kontrol yang tidak didukung ditolak. Probe ketersediaan sanitizer positif yang dikenal tetap diizinkan.

Regresi memakai salinan disposable untuk mutasi penghapusan ledger+manifest serta penggantian kedua kondisi stdout fixed-array (native biasa dan sanitizer) dengan `if false; then`. Regresi tambahan mencakup sanitizer saja, bukti deklarasi, komentar/heredoc, fungsi tak dipanggil, wrapper false/negasi/compound dengan variasi whitespace, penghilangan capture, dan penghilangan exit gagal. Runner doc-test menjalankan seluruh regresi validator melalui discovery.

## Validasi

Regresi validator: **41 tes lulus**, termasuk kedua mutasi asli. PRD doc-test: **17 fence terklasifikasi**, dua blok executable diperiksa (satu lolos dan satu penolakan yang diharapkan), 13 proposal dan dua teks dilewati sesuai klasifikasi, tanpa fence tak berlabel. Build Clang Release bersih pada snapshot disposable dan seluruh suite `run_all_tests.sh` lulus. Review independen akhir menyetujui kedua mitigasi setelah temuan wrapper bernegasi diperbaiki dan diregresikan. Pemeriksaan CI Linux/Windows dilakukan pada head PR akhir; hasilnya dilaporkan terpisah agar tidak memerlukan commit baru hanya untuk memperbarui hash CI. Tidak ada perubahan perilaku bahasa, compiler, fixture semantik, atau runner native dalam mitigasi ini.

## Batasan

Crosswalk merupakan daftar sumber yang dipelihara melalui review, bukan bukti bahwa setiap atom PRD telah ditemukan. Perubahan terkoordinasi pada ketiga daftar atau persyaratan baru tanpa atom tetap memerlukan audit G2 lengkap. Pemeriksaan shell sengaja dibatasi pada struktur runner yang dikenal; bukan interpreter Bash atau pembuktian reachability sewenang-wenang. Ketersediaan sanitizer pada mesin tetap menentukan apakah jalur itu dieksekusi oleh suite native. Kecocokan bukti tidak membuktikan kecukupan semantik fixture. Suite compiler dan review independen tetap diperlukan. G2 **belum ditutup**; label dan jumlah ledger tidak berubah oleh mitigasi ini.
