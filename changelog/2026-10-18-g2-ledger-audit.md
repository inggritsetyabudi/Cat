# G2 — audit atom, scope dan bukti baseline

## Koreksi

Ledger memakai baseline main `e7789c6efedff677b81c2f883fe8498e32c30554`, bukan implementasi pada PR #22–#26. Setiap ID dipetakan pada `cat-northstar/feature-status-atoms.json` ke heading sumber dan, untuk irisan diterima, fixture serta assertion runner yang benar-benar ada. Ringkasan hitungan final berada di ledger; hitungan changelog historis 92 ID bukan hitungan inventaris yang diperluas ini.

- `G2-PRD-13-CONTRACT` kini memakai `revise_implements.cat` dan assertion native `localhost`, bukan fixture `test_implements.cat` yang tidak dijalankan. Penerimaan hanya methods same-module. Generic `with`, locality methods, locality contract, third-module rejection dan duplicate pair mempunyai ID keputusan terpisah.
- Nonzero integer dibatasi pada `-20 % 6` dan `48 / 4 / 3` yang diasert runner. Integer zero-divisor tetap keputusan belum diterima main; PR23 tidak dikreditkan.
- Try initializer positif, larangan `var`, larangan return-expression, serta nonterminal `else` dipisah. Fixture negatif menyertakan assertion diagnostik nyata. Identitas error nominal lintas modul hanya mengklaim kasus mismatch yang dipetakan, bukan seluruh generic identity.
- Radix memiliki bukti native dan konstanta LLVM terpilih. Ordinary string escape memiliki perbandingan byte native dan penolakan frontend yang relevan. Keduanya bukan penerimaan seluruh matriks literal/Unicode.
- Float32/Float64 hanya diterima untuk unary negation, `fneg`, signed zero dan stdout fixture. Materialisasi literal Float32/Float64 dan Float128 tetap ID keputusan terpisah; PR24 tidak dikreditkan.
- Assertion array OOB kini menunjuk pemeriksaan proses gagal, bukan output sanitized fixture positif. Bukti modul menunjuk diagnostik alias/cycle/path yang tepat, bukan diagnostik double-main yang tidak terkait.
- Slice inisialisasi dipersempit ke penolakan compound assignment local/field sebelum inisialisasi. Fixture itu tidak membuktikan seluruh analisis definite-init pada branch/loop.
- Penamaan `Show` hanya satu ID kanonik di §20. Grammar f-string tetap terpisah. Default, Send dan Sync masing-masing mempunyai ID.

## Atom independen

Ditambahkan aturan collection keywords, inferensi kosong, homogenitas, hash, equality dan const N; wildcard terakhir/unreachable warning; satu try/satu Result; perbandingan non-asosiatif, `is` untuk ref dan bitwise integer; Char range, singleton tuple, Unit dan wildcard drop; elision ref; Float128; String clone/move/temporary view/storage; komentar `///` dan `claw doc`; tiga annotation builtin; noalias; checked/wrapping/saturating; konversi penyempitan; closure borrow/move/return; serta penamaan static.

Teks mengikuti keputusan normatif yang nyata: misalnya `return` keluar dari closure, nama konstanta static huruf kecil, `as` integer debug defect/release truncate, float-to-int saturasi/NaN 0. Aturan tidak ditambahkan hanya dari dugaan perilaku umum bahasa lain. PLAN:G dipetakan ke tabel perubahan global §2; PLAN:M ke milestone v0.1–v1.0 §4, bukan roadmap pasca-v1.

## Batas dan validasi

G2 tetap **NOT CLOSED**. Manifest memeriksa integritas inventaris yang tercatat, bukan membuktikan bahwa setiap aturan PRD telah ditemukan; menghapus atom dari ledger dan manifest sekaligus tetap memerlukan review untuk dideteksi. Scope keputusan yang belum diaudit tidak disamakan dengan compiler menolak semua bentuknya. Hasil focused tests, doc-tests, suite lengkap disposable Release, review independen dan CI Linux/Windows pada final head dicatat pada PR #27.

Tidak ada perubahan compiler/runtime, Arena, f-string implementation, workflow CI, PR #22–#26, merge, versi, tag atau release. Worktree pemilik dan artefak dirty yang sudah ada dipertahankan; eksekusi suite penuh dilakukan pada salinan disposable.
