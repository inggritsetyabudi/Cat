# G2 — sumber, kepemilikan aturan, dan bukti PRD §§6–10

## Hasil koreksi

Melanjutkan PR #27 pada `docs/g2-feature-status-20261018` dari `dde2412f2df12de6a62ee1a1d23b59bd2e2f9442`. Audit baca-saja meninjau seluruh PRD §§6–10 sebelum perubahan. Koreksi ini hanya menyentuh ledger, source index, evidence manifest, dan changelog ini. PRD, compiler, runtime, validator, fixture, dan runner tidak diubah; koreksi §§0–5 sebelumnya dipertahankan. **G2 tetap NOT CLOSED.** Audit bagian ini tidak mengklaim inventaris seluruh PRD exhaustive atau fitur selesai.

Ketiga inventaris sekarang berisi **216 ID unik yang sama**: **58 [diterima compiler]**, **158 [diputuskan]**, **0 [diusulkan]**. Ada **27 ID baru**, **30 baris lama dikoreksi**, dan **tidak ada ID dihapus**. Dua ID baru diterima hanya untuk bukti yang sudah ada: proyeksi plain pada `revise_tuples.cat`, serta check base destructuring plain dan satu negative source-reuse. Tidak ada promosi penerimaan integer divisor nol, MIN/-1, generic iterable, full dataflow, atau timing drop.

## §6 — operator dan aritmetika

[Sumber normatif §§6](../cat-northstar/PRD.md#6-operator-presedensi-asosiativitas): `BITWISE-INT` memakai L329 termasuk `<<`/`>>`; `DIV-MIN` L319; `DIV-NONZERO` tabel L293; comparison `NONASSOC` L299/L308; `SHORT` L309. `PRECEDENCE` kini memuat tabel tepat L290–303 dan aturan bitwise di atas comparison L307, bukan heading L286. Urutan binding, ordinary associativity, range non-associativity, assignment kanan, dan assignment sebagai statement mempunyai pemilik berbeda.

`OP-GROUP` tidak lagi membundel compound update, precedence/associativity, dan boolean semantics. Penerimaannya hanya hasil akhir urutan compound local Int32 (output pertama `-2`) dan field Meter (output ketujuh `5`) di fixture A/B. `DIV-NONZERO` hanya dua hasil lain, output kedua `-2` dan keempat `4`. `SHORT` memiliki jalur skip/evaluate RHS fixture A/B (dua output `99`) dan fixture ownership yang mempertahankan `held`. Assertion stdout gabungan yang sama boleh menopang irisan berbeda ini karena ekspresi dan posisi hasilnya dicatat; itu bukan penerimaan ganda kewajiban yang sama. Contoh precedence/left-associativity lain di fixture tidak menaikkan keseluruhan tabel menjadi diterima.

`COMPOUND` dibatasi secara eksplisit pada **lowering IR**: satu call `next_index` pada `values[next_index()] += 1`. Fixture bahasa tidak mempunyai `main`; runner juga menyediakan C harness dan menjalankannya, tetapi label atom ini tetap hanya mengkreditkan assertion hitungan IR, bukan bukti universal evaluasi runtime atau setiap receiver/operator.

Keputusan divisor nol L315–319 dipisah menjadi runtime policy integer `/`/`%`, compound `/=`/`%=`, konteks literal/konstan/dinamis, diagnostik/penghentian tanpa jaminan cleanup, larangan operand LLVM invalid, dan float IEEE-754. Ekspresi yang tidak dievaluasi merujuk pemilik short-circuit; batas konteks lama termasuk static literal-only tidak dilonggarkan. MIN/-1 tetap mengikuti kebijakan §19 dan belum diterima pada main. PR23 tidak dikreditkan. Staging A–D L327–330 dan tes tabel L338 adalah dimensi pengiriman, bukan acceptance operator; Pratt L332 merujuk `G2-PRD-0-TABLE-DRIVEN`, sedangkan overflow umum L332/L339 tetap milik §19/§23.

## §7 — Range, scan, dan lexer

[Sumber §§7](../cat-northstar/PRD.md#7-range-dan-scan-item-i): aturan endpoint L347, tipe `Range[T]` L347, satu tipe untuk iterasi/`in`/slicing L348, dan `scan` L349 tidak lagi tersembunyi dalam satu klaim. `SCAN` hanya menerima index `USize` mulai nol untuk fixture `Range[Int32]` melalui parameter `index_code(USize)` dan pasangan `3/100, 4/101, 5/102`. Kewajiban iterable apa pun tetap keputusan tersendiri; pembatasan compiler terhadap indexed scan `Str` bukan perubahan PRD.

Lexical dot-after-digit L360 dan matriks float/range/trailing-dot/projection L368 dipetakan, dengan token `..=` memiliki atom sendiri. Rantai proyeksi `x.0.1` merujuk pemilik lexical §8 L392, bukan diduplikasi. Acceptance iterasi tidak dianggap acceptance tokenisasi. `U8` memakai L361 dan hanya menerima observasi terminasi/count pada maksimum 255 (termasuk count 256 dengan timeout). Urutan algoritma check-before-increment tetap keputusan tersendiri; output saja tidak membuktikan struktur algoritma. LLVM `phi` L362 dicatat sebagai dimensi implementasi, bukan semantik baru. Slicing tetap mempunyai pemilik endpoint/safety §14.

## §8 — tuple

[Sumber §§8](../cat-northstar/PRD.md#8-tuple): `PLAIN` hanya literal berkoma vs grouping tanpa koma (L377/L391). Tipe tuple, proyeksi numerik L378, lexer proyeksi L392, base destructuring, nested destructuring dan tuple-pick L379 dipisahkan. ID historis `SINGLETON-UNIT` dipertahankan tetapi hanya memiliki singleton `(a,)`; Unit tetap hanya `UNIT`. Keduanya tidak dipromosikan dari kehadiran contoh positif di fixture umum.

`OWNED` dikoreksi dari L379 ke konsumsi tuple L380. Acceptance hanya transfer/cleanup tuple `Anchor[Int32]` pada fixture dengan stdout dan ASan/LSan; tidak membuktikan seluruh source-reuse negatif atau graph ownership. `WILDCARD-DROP` hanya timing pembebasan `_` pada statement yang sama, tetap keputusan: marker `after-multi-wildcard` dan ASan bersih tidak membuktikan kapan `free` terjadi. Partial/nested owned/view/generic dan assignment mengikuti batas L379; PR22 tidak dikreditkan. Lint tuple lebih dari tiga elemen L400 memiliki atom keputusan tersendiri; layout LLVM struct L393 bukan penerimaan semantik umum.

## §9–10 — borrow dan initialization

[Sumber §§9](../cat-northstar/PRD.md#9-lifetime-klaim-yang-akurat-dan-dataflow): `DATAFLOW` memiliki last-use L408/L410 saja; mutable overlap, owner move, dan owner mutation L410 terpisah dan tetap keputusan. Aturan tanpa anotasi lifetime bergaya apostrof dan gerbang suite negatif Safe L428 juga dipetakan, bukan disamakan dengan penerimaan compiler. Staging per-blok ke dataflow L427 serta pencocokan dengan sema L429 tetap tugas implementasi/review.

`BORROW-NEG` tidak lagi mengutip `ref mut[s]` L420: ia memakai konteks owner/borrow L410 dan suite dangling L428, hanya untuk penolakan discard borrowed view dari temporary owner dengan diagnostik exact fixture. Ini bukan kutipan larangan temporary-discard literal yang tidak ada pada PRD, juga bukan penerimaan seluruh dataflow. `ELISION-MULTI` memakai seluruh contoh dan komentar ambigu L417; single-ref L415 dan batas `ref mut[s]` L420 tetap keputusan.

[Sumber §§10](../cat-northstar/PRD.md#10-inisialisasi-default): `READ` dan `ASSIGN` memakai aturan L435, bukan status historis L456. Acceptance masing-masing hanya local compound `count` dan field compound `meter` yang belum diinisialisasi; field mensyaratkan tepat satu diagnostik. `LATER-ASSIGN` hanya check contoh linear PRD L439–445, bukan nilai runtime atau all-path definite-init. API tertunda `Tipe.default()` L454 mempunyai pemilik tersendiri yang merujuk keputusan penamaan kanonik `G2-PRD-20-DEFAULT`; baris §20 itu tidak diubah atau diduplikasi.

## ID yang berubah

Awalan setiap daftar di bawah adalah `G2-PRD-`.

**30 ID lama dikoreksi:**

- §6: `6-BITWISE-INT`, `6-COMPOUND`, `6-DIV-MIN`, `6-DIV-NONZERO`, `6-DIV-ZERO`, `6-IS-REF`, `6-NONASSOC`, `6-OP-GROUP`, `6-PRECEDENCE`, `6-SHORT`.
- §7: `7-CHAR`, `7-RANGE`, `7-SCAN`, `7-U8`.
- §8: `8-NESTED`, `8-OWNED`, `8-PARTIAL`, `8-PICK`, `8-PLAIN`, `8-SINGLETON-UNIT`, `8-UNIT`, `8-WILDCARD-DROP`.
- §9: `9-BORROW-NEG`, `9-DATAFLOW`, `9-ELISION-MULTI`, `9-ELISION-SINGLE`, `9-REFMUT`.
- §10: `10-ASSIGN`, `10-READ`, `10-LATER-ASSIGN`.

**27 ID baru:**

- §6: `6-ASSOCIATIVITY`, `6-RANGE-NONASSOC`, `6-ASSIGN-RIGHT`, `6-ASSIGN-STATEMENT`, `6-DIV-ZERO-COMPOUND`, `6-DIV-ZERO-CONTEXT`, `6-DIV-ZERO-TERMINATION`, `6-DIV-ZERO-LLVM`, `6-FLOAT-IEEE`, `6-OP-STAGING`.
- §7: `7-RANGE-TYPE`, `7-RANGE-USES`, `7-SCAN-ANY-ITERABLE`, `7-LEXER-DOT`, `7-LEXER-INCLUSIVE`, `7-RANGE-INCLUSIVE-STEP`.
- §8: `8-TYPE`, `8-PROJECTION` (**diterima terbatas**), `8-PROJECTION-LEXER`, `8-DESTRUCTURE-BASE` (**diterima terbatas**), `8-LINT-SHAPE`.
- §9: `9-MUTABLE-OVERLAP`, `9-OWNER-MOVE`, `9-OWNER-MUTATE`, `9-NO-LIFETIME-ANNOTATION`, `9-SAFE-SUITE`.
- §10: `10-DEFAULT-API`.

## Validasi dan batasnya

- Validator ledger/source-index/evidence manifest lulus; 216 ID unik identik pada ketiganya. Seluruh entry/baris di luar §§6–10 diperiksa tetap identik dengan baseline, selain ringkasan hitungan ledger.
- `run_doc_tests.sh`: **41 regresi lulus**. Doc-test: 17 fence diklasifikasikan, dua executable (satu lolos dan satu expected rejection), 13 proposal serta dua text dilewati, nol tanpa label.
- Focused native menjalankan **10 blok fixture asli**: tuple literal/projection, nested destructuring, owned destructuring, tuple pick, Int32 range, UInt8 range, Int32 range scan, operator A/B, short-circuit ownership, dan index compound (termasuk IR call-count, native C harness, serta OOB expected nonzero trap).
- Sanitizer terkait lulus: owned tuple ASan/LSan serta nested tuple ASan/UBSan/LSan, berikut exact stdout masing-masing. Tidak ada inferensi statement-time drop dari hasil itu.
- Tambahan frontend mengeksekusi semua assertion accepted frontend §§6–10: base destructuring plain, negative source-reuse plain, temporary borrowed discard, local compound-uninitialized, dan field compound-uninitialized dengan hitungan diagnostik. Comparison-chain, owned alias-reuse, dan owned projection-move juga diuji negatif tanpa menaikkan label keputusan umum.
- Governance fence terfokus lulus untuk execute/diagnostic/skip/count; label kosong, label tak dikenal, dan fragmen diagnostik salah masing-masing gagal seperti diharuskan. Ini bukti runner, bukan acceptance bahasa menyeluruh.
- Binary Linux Release yang tersedia dipakai ulang setelah SHA-256 membuktikan identitas 43 file `claw/src`, satu runtime, dan `CMakeLists.txt` terhadap snapshot build. Semua artefak tes berada di lokasi bukti terpisah; worktree lama beserta artefak kotornya tidak dibersihkan.
- `git diff --check` lulus. Tinjauan independen baca-saja dilakukan atas koreksi ini; hasilnya tidak mengesahkan penutupan G2.

Sisa utama: tabel parser/associativity lengkap, integer zero dan MIN/-1 menurut profil, float IEEE edge, bitwise dan `is`, domain Range/Char dan konsistensi tiga konsumennya, generic iterable, tokenisasi exact, algoritma inclusive-step umum, singleton/Unit, drop timing, seluruh owner/borrow CFG dan elision, all-path definite-init, serta API default eksplisit. Full compiler suite, Linux/Windows CI pada head final, dan audit bagian PRD lain tidak dijalankan/ditutup oleh koreksi ini. Tidak ada perubahan Arena/f-string, PR #22–#26, merge, tag, version atau release. **PR #27 tetap terbuka dan G2 NOT CLOSED.**
