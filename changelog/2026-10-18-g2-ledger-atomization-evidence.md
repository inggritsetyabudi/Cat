# G2 — atomisasi ledger dan koreksi bukti

## Perubahan terbatas

Melanjutkan PR #27 dari head `2429010e5054f3991fba13645c70b3c98a2640b4`. Hanya ledger, manifest atom, dan changelog ini yang berubah. Baseline implementasi tetap `e7789c6efedff677b81c2f883fe8498e32c30554`; perubahan terdahulu dipertahankan. **G2 tetap NOT CLOSED**, dan inventaris ini tidak diklaim exhaustive.

Hitungan ledger berubah dari 147 menjadi **164 ID**: **53 diterima compiler**, **111 diputuskan**, dan **0 diusulkan**. Label diterima hanya berlaku untuk irisan fixture dan assertion yang tertaut, bukan fitur penuh.

## Aturan yang dipisahkan

- Shape: `G2-PRD-1-SHAPE-FIELDS`, `G2-PRD-1-SHAPE-DUPLICATE`, dan `G2-PRD-1-SHAPE-TYPE`, bersumber pada [PRD §1, baris 90](../cat-northstar/PRD.md#1-konstruksi-shape-dan-named-arguments). Baris `G2-PRD-1-SHAPE` tetap khusus locality. Hanya mismatch field generic `Name[outer]`/`Name[inner]` diterima; kelengkapan dan duplikasi tetap keputusan.
- Init: `G2-PRD-10-LATER-ASSIGN`, dari [PRD §10, baris 435–445](../cat-northstar/PRD.md#10-inisialisasi-default). Fixture embedded `cat` mendeklarasikan `count` tanpa initializer, mengisinya, lalu membaca lewat return; `run_doc_tests.py` mengharuskan `check` sukses. Ini bukan bukti nilai runtime atau seluruh definite-init CFG.
- String: `G2-PRD-12-STRING-OOM`, dari [PRD §12, baris 498](../cat-northstar/PRD.md#12-string-interpolasi-dan-komentar-dokumen), tetap keputusan; cleanup normal tidak membuktikan penghentian deterministik saat OOM.
- Slicing: `G2-PRD-14-BOUNDS`, `G2-PRD-14-UTF8`, `G2-PRD-14-BORROW`, dan `G2-PRD-14-LEGACY-SLICE`, dari [PRD §14, baris 556–559](../cat-northstar/PRD.md#14-slice). `G2-PRD-14-SLICE` hanya sintaks/hasil indexing range; `G2-PRD-14-GET` kini khusus hasil `Maybe` untuk range invalid. Borrow diterima hanya untuk penolakan hasil `get` dari owner lokal; penghapusan method publik hanya untuk `Str.slice` yang ditolak frontend.
- Konfigurasi: `G2-PRD-15-CONFIG-LINK` dan `G2-PRD-15-CONFIG-DUPLICATE`, dari [PRD §15, baris 587](../cat-northstar/PRD.md#15-foreign-c-annotation-dan-rewrite), keduanya keputusan. Keduanya bukan klaim yang tersirat dalam baris boundary raw `G2-PRD-15-FFI`.
- Modul: `G2-PRD-17-SHARE`, `G2-PRD-17-NAMESPACE`, `G2-PRD-17-DIRECT-ITEM`, dan `G2-PRD-17-BASENAME-ALIAS`, dari [PRD §17, baris 625–629](../cat-northstar/PRD.md#17-modularitas). Penerimaan dibatasi pada penolakan import `hidden`, namespace beralias, import `add as sum`, serta dua basename `math` dengan alias berbeda. Penolakan alias duplikat tetap kanonik pada `G2-PRD-17-CASE`, bukan diduplikasi menjadi baris negatif baru.
- Implicit return: `G2-PLAN-M02-IMPLICIT-TAIL` dan `G2-PLAN-M02-IMPLICIT-WARNING`, bersumber normatif pada [PRD-CHANGES §3, baris 76](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#3-perubahan-per-bagian-prd). Manifest memetakan kategori `PLAN:M02` ke [milestone v0.2 di §4](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#4-milestone-final-v01-sampai-v10), sesuai schema validator yang tidak diubah; tautan normatif §3 dicatat terpisah. Tail diterima lewat stdout `42` dan diagnostik fungsi bertail deklarasi. Warning bukan disimpulkan dari stdout.

## Bukti dan residual

`G2-PRD-17-SUPER` dikoreksi: runner baris 305 adalah `run_check_pass` untuk fixture positif sibling `src/service.cat` → `src/math.cat`, **bukan assertion diagnostik**.

Fixture gabungan tidak disamarkan sebagai tes terisolasi. Output `revise_str_range_views.cat` dibagi secara eksplisit antara sintaks/hasil range dan enam hasil `none` pada `get`; fixture `revise_module_aliases` dipakai bersama untuk namespace beralias dan jalur positif basename kembar.

Bounds direct memiliki helper dinamis `run_expect_str_range_defect`; warning memiliki helper `run_check_warning`. Assertion dan fixture aslinya dicatat dalam manifest, dan focused tests menjalankannya. Keduanya tidak dinaikkan menjadi penerimaan atom hanya dengan memanfaatkan assertion yang sebenarnya milik tes lain: validator saat ini tidak memasangkan helper dinamis/warning tersebut sebagai penerimaan. Label bounds dan warning tetap keputusan. UTF-8 direct juga tetap keputusan karena fixture defect direct hanya menguji endpoint kanan invalid, bukan matriks kedua endpoint. Hasil `Maybe` pada `get` tidak digeneralisasi menjadi bukti defect indexing langsung.

Residual mencakup kelengkapan/duplikasi field, konfigurasi, fault injection OOM, perluasan matriks lifetime dan UTF-8 direct, serta pemetaan assertion tersebut. Bukti yang belum dikreditkan tidak berarti compiler pasti menolak atau belum mengimplementasikannya.

## Validasi

- Validator ledger/manifest yang tidak diubah: lulus.
- Suite `test_feature_status.py`: 20 tes lulus.
- `run_doc_tests.sh`: 17 fence diklasifikasikan, dua executable diperiksa (satu sukses dan satu expected rejection); 13 proposal serta dua text dilewati, nol fence tanpa label.
- Focused tests memakai ulang helper dan assertion runner asli: tipe field scoped, visibilitas private, namespace/direct import, alias basename, `super` positif, borrow hasil `get`, penghapusan `Str.slice`, implicit-tail positif/negatif/warning, stdout serta guard LLVM range, dan lima fixture defect range. Lulus; proses defect gagal sesuai assertion runner.
- Binary Linux Release yang tersedia dipakai ulang setelah source `claw/src`, runtime, dan CMake dicocokkan identik dengan worktree ini. Artefak focused tests ditulis di direktori bukti terpisah, bukan menimpa artefak worktree pemilik.

Tidak ada perubahan compiler, runtime, validator, test runner/fixture, semantik PRD, PR lain, merge, atau release. Tidak dibuat tes fitur baru untuk atom keputusan; fault injection OOM, matriks penuh, full compiler suite, dan eksekusi Windows tidak dijalankan dalam koreksi dokumentasi terfokus ini. CI/review pada head final tetap gerbang terpisah, bukan alasan menutup G2.
