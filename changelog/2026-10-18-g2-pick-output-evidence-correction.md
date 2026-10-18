# Koreksi bukti output PICK pada ledger G2

Baris `G2-PRD-3-PICK` membedakan input fixture dari output yang benar-benar ditegaskan runner. Tiga pemanggilan menggunakan input `left(606)`, `right(707)`, dan `right(808)`, tetapi hasilnya adalah **606, 0, -1**, bukan 606, 707, 808.

[Fixture](../claw/tests/native/fixtures/revise_if_pick_values.cat) mendefinisikan wildcard expression dengan nilai `0` (L88–97), wildcard statement dengan assignment `-1` (L98–109), dan tiga pemanggilan di L144–146. [Runner native](../claw/tests/native/run_native_tests.sh) membangun fixture dan membandingkan stdout di L1047–1053; nilai ekspektasi pada L1050 mencantumkan urutan 606, 0, -1.

Label `[diterima compiler]` tetap hanya untuk irisan fixture bernilai pada return, initializer, dan cabang terminal yang diuji. Koreksi ini tidak membuktikan penolakan non-exhaustive, seluruh matriks pattern, atau penerimaan fitur lain. Tidak ada perubahan compiler, sintaks, PRD, maupun fixture.

Manifest mempertahankan tautan assertion stdout yang sudah benar; string ekspektasi persis dikunci oleh regresi khusus. Regresi `test_feature_status_pick_evidence.py` memeriksa input, perilaku wildcard, redaksi ledger, dan string ekspektasi runner. Source index mempertahankan kutipan normatif PRD yang sama: tidak ada salinan klaim output salah di sana.
