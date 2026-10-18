# G2 validator — feature atom manifest

## Perubahan

Validator dokumentasi kini memerlukan `cat-northstar/feature-status-atoms.json`, sebuah object keyed by ledger ID dengan `source_anchor`, `fixtures`, dan `assertions` (`path` dan exact `text`). ID manifest harus tepat sama dengan ID ledger. Source anchor harus menunjuk heading aktual dengan nomor section yang cocok pada `cat-northstar/PRD.md` untuk `PRD:n`; `PLAN:G1..G8` dipetakan ke tabel global §2 dan `PLAN:M01..M10` ke tabel milestone §4 pada PRD-CHANGES (M10 memakai `v1.0`), bukan roadmap pasca-v1. Anchor sumber wajib tertaut dalam ledger. Path fixture/runner dicocokkan setelah resolusi path penuh dan tetap berada dalam repositori, bukan hanya dicocokkan basename.

Untuk irisan `[diterima compiler]`, validator memeriksa fixture yang ada dan tertaut oleh bukti ledger, runner yang ada, exact assertion text, referensi basename fixture pada runner, serta kecocokan bounded test block bila dapat ditentukan. Tes temporary-repository mencakup heading salah, atom hilang, fixture mismatch termasuk basename kembar, runner tidak tertaut, schema array/item korup, array accepted kosong, dan assertion mismatch.

Fixture frontend dan native dapat memiliki runner berbeda; tiap fixture wajib terhubung ke setidaknya satu assertion pada runner terkait, bukan setiap fixture diwajibkan muncul pada setiap runner. Entrypoint workspace `main.cat` dapat dipanggil melalui nama direktori fixture. Assertion multibaris dengan diagnostik shell yang dilanjutkan memakai backslash diperiksa tanpa mengubah teks sumber.

Pairing pada runner shell memakai invocation compiler atau helper tes terdekat, bukan sekadar satu fungsi/top-level file yang luas. Assertion harus terkait setidaknya satu fixture atom dan sebaliknya. Regresi mencakup assertion yang benar-benar ada tetapi milik tes berbeda, command multibaris, dan sumber malformed `PRD:x`/`PLAN:Gx`/`PLAN:Mxx` tanpa traceback. Suite terfokus kini berisi 20 tes. Fixture gabungan tetap boleh mendukung beberapa atom bila ledger menjelaskan bagian output dan cross-reference secara eksplisit; ini tidak disamarkan sebagai tes terisolasi per atom.

## Batasan

Validasi ini bersifat leksikal dan struktural. Ia tidak dapat membuktikan bahwa fixture benar-benar menguji semantik atom atau bahwa assertion cukup kuat; tinjauan independen tetap diperlukan. Penghapusan serentak manifest dan ledger secara inheren tidak dapat dideteksi validator; compiler tests tidak dijalankan.
