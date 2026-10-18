# G2 — koreksi sumber dan atom PRD §§11–20

## Hasil utama

Melanjutkan PR #27 pada `docs/g2-feature-status-20261018` dari `20bbc107228b85f48a7d26a9a76900943f10f55c`, menggunakan worktree baru tanpa membersihkan worktree atau artefak lama. Audit ini menelusuri aturan, contoh normatif, mekanisme compiler, serta penutup risiko §§11–20. Hanya ledger, source index independen, manifest bukti, dan changelog ini diubah. PRD, compiler, runtime, validator, fixture dan runner tetap identik; koreksi §§0–10 serta seluruh entry PLAN di luar lingkup dipertahankan. **G2 tetap NOT CLOSED.**

Ketiga inventaris kini memiliki **299 ID unik yang sama**: **54 [diterima compiler]**, **245 [diputuskan]**, **0 [diusulkan]**. Ada **91 ID baru**, **66 ID lama dikoreksi**, dan **8 ID dihapus/digabung**. Tidak ada penerimaan fitur baru dari PR22–PR26. Label accepted berkurang dari 58 menjadi 54: dua klaim salah-sumber dihapus (unary fneg dan index scalar Str), collision alias ganda digabung, dan nama modul UpperMath tidak lagi dipakai untuk menerima aturan satu file=satu modul. Bukti inherent method dipindahkan ke METHOD-LOCALITY tanpa memperluas slice; atom baru tetap keputusan.

## Koreksi per bagian

**[§11 — literal](../cat-northstar/PRD.md#11-literal-numerik-dan-char).** CHAR memakai aturan scalar L469 dan hanya menerima dua negative escape surrogate/out-of-range beserta diagnostiknya. Width 32-bit, UTF-8 decode, escape, decimal separator, radix binary/octal, suffix recognizer/canonical/fallback/error dan literal range dipisah. RADIX dibatasi pada hasil hex terpilih dan assertion IR/stdout yang sudah ada. Float128 parse APFloat dan representasi fp128 terpisah, keduanya keputusan; materialisasi literal bukan unary fneg. ID FLOAT lama dihapus karena sumber suffix §11 tidak mendukung unary. Ini tidak menyatakan implementasi fneg hilang atau gagal.

**[§12 — String dan dokumentasi](../cat-northstar/PRD.md#12-string-interpolasi-dan-komentar-dokumen).** Alias, layout/size/alignment, immutable/move-only, literal type/static-storage/noalloc/nocopy, konversi, as_str, clone, move/owned-parameter/return, cleanup, Str destructor/free, OOM, inference/escape/owner-move/mutate, temporary direct/store/return/if-join, shape/container/choice/Maybe carrier, biaya statis, diagnostik dan terminal policy dipisah. Acceptance lama hanya view lokal, cleanup temporary dan escape ordinary string yang benar-benar diassert. STR-INDEX tidak memiliki aturan index scalar pada §12 dan dihapus, bukan dipindahkan menjadi aturan slicing baru. `///` attach, non-share warning, orphan warning dan `claw doc` tetap atom terpisah. F-string, builder serta lexer nested-brace/quote tetap tertunda. Tidak ada aturan format Show yang diciptakan.

**[§13 — generic dan contract](../cat-northstar/PRD.md#13-generic-contract-with-dan-lokasi-implements).** Fixture `revise_implements.cat` ternyata berisi `implements Config` tanpa `with`; stdout localhost tidak menerima orphan contract. Bukti dipindahkan dari CONTRACT ke METHOD-LOCALITY untuk kasus positif modul yang sama saja. Contract locality, third-module error, duplicate pair, keyword with/non-colon, optional constraint, single/and, tipe kontrak generic penuh dan pemeriksaan graf modul memakai kutipan tepat L519/L527/L529–533. OPERATOR tidak lagi mengarang hukum overloading dari contoh Add[Vector2].

**[§14 — slicing](../cat-northstar/PRD.md#14-slice).** Range indexing Str dan get memiliki scope native fixture AéB yang eksplisit. Bounds, endpoint UTF-8, unit byte, borrow owner, penghapusan Str.slice publik, target lowering internal, larangan Str.from/to_owned serta contoh Span tidak dibundel. BORROW tetap hanya negative local-return fixture; LEGACY-SLICE tetap hanya negative Str.slice fixture. Direct bounds/UTF-8 tetap keputusan meskipun fixture defect existing dijalankan. Teks L558 «hasil selalu Str» dan contoh Span L552 dicatat sebagai residual konteks non-string, tidak diatasi dengan semantik baru.

**[§15 — FFI/annotation](../cat-northstar/PRD.md#15-foreign-c-annotation-dan-rewrite).** Sintaks foreign c, satu external-contract structure, raw-call, safe-wrapper, TOML link-only dan signature ganda dipisah. Token @, daftar builtin tertutup, kewajiban perubahan PRD untuk tambahan, generator constructor dan locality generator terpisah; test/deprecated hanya nama builtin, tanpa mengarang perilaku. Rewrite tetap v1.x, bukan v1; postfix ! dipisah dari deferral.

**[§§16–18 — sumber, modul dan governance](../cat-northstar/PRD.md#16-sinkronisasi-dua-prd).** RATIFY lama salah mengutip normalisasi encoding dan dihapus. SOURCE hanya otoritas PRD; generated Ringkas/UTF8 merujuk PLAN-G7, main/varian PLAN-G6, Arena §19-ARENA, tanpa label duplikat. Pada §17 fixture UpperMath hanya casing, sehingga MODULE kini keputusan pemetaan file/modul; casing tetap §4-LOWERCASE-IDENT. CASE digabung ke BASENAME-ALIAS dengan positive+negative assertion exact. File/folder, no modules.cat, no pub, src-other dan LLVM symbol/share/private/merge dipisah. Milestone §18 merujuk PLAN-M01–M10; V01/V10 ganda dihapus. Safe, Simple, boilerplate, Fast threshold, no-hidden-cost, proof-safe elision dan self-hosting prerequisites dilacak tanpa menerima gerbang kualitas dari compiler test.

**[§§19–20 — keputusan penutup](../cat-northstar/PRD.md#19-keputusan-dari-celah-di-luar-proposal).** Quote `---` untuk bounds diperbaiki; bounds retention dan proof-safe elision punya pemilik berbeda. Overflow release/debug serta checked/wrapping/saturating eksplisit tidak digabung. Noalias mengutip keseluruhan kewajiban pointer LLVM L662, bukan kutipan terputus sebelum atributnya. Arena hanya audit v0.9 dan koreksi contoh; klaim eksperimental-v1/fixed-pool yang tidak didukung §19 dibuang. Narrowing integer debug/release, float range/NaN/LLVM, Char.from dan x.to[Int8] punya scope tepat; closure capture-ref/move/return tetap terpisah. Maybe type/Some/None ditambahkan. Static hanya acceptance jalur positif nama kecil dan stdout fixture, bukan negatif casing.

Pemeriksaan exact baseline membuktikan **tidak ada G2-PRD-12-SHOW** dan **Default/Send/Sync sudah terpisah** sebelum koreksi ini. Jadi dugaan historis duplikasi tidak direkayasa menjadi perubahan palsu. G2-PRD-20-SHOW tetap satu-satunya keputusan nama kontrak konversi teks, bukan formatting semantics atau implementasi interpolasi. THREAD dihapus karena §20 hanya menamai Send/Sync, tidak menetapkan staging runtime threading; roadmap existing tidak diubah. Pengulangan Fast §20 merujuk §18-FAST.

## Validasi dan batas bukti

- Validator ledger/source-index/manifest lulus. Semua row dan structured entry di luar §§11–20 diperiksa identik dengan baseline; ringkasan hitungan/residual dan rujukan lintas bagian diperbarui.
- `run_doc_tests.sh`: validator dan **41 regresi** lulus; **17 fence** diklasifikasikan, **2 executable** diperiksa (1 pass, 1 expected rejection), **13 proposal** dan **2 text** dilewati, **0 tanpa label**. Kategori skip tidak menjadi bukti bahasa.
- Suite frontend existing lulus: **78 check/pass**, **294 expected check/fail**, **11 AIR pass**, **7 expected AIR fail**, **1 validate/pass**, **2 expected validate/fail**. Seluruh assertion accepted §§11–20 tercakup, termasuk negative borrow, legacy API, alias/cycle/private dan positive namespace/package/super.
- Suite native existing lulus: **75 build/pass**, **7 expected build/fail**, **6 ASan pass**, **11 ASan/UBSan/LSan pass**, **2 sanitizer pass**. Ini mencakup numeric literal/Char, ordinary string escapes, owner/clone/move/param/return, recursive cleanup, temporary, Str range valid/invalid dan static. OOB trap nonzero yang diharapkan bukan kegagalan suite.
- Runner disalin dengan redirect path output saja ke lokasi bukti di luar repo; assertion/fixture/compiler semantics tidak diubah. Binary Linux Release dipakai ulang setelah SHA-256 membuktikan identitas **45 file sumber/build-input** (43 `claw/src`, satu runtime, satu CMakeLists) terhadap snapshot build. SHA-256 binary: `583b1b0fe4afa198e619687be33ec297b64920ab320c041b055062d31b4fe64e`.
- Upaya terpisah memanggil CLI Python doc-test dengan argumen direktori gagal setup dan tidak dikreditkan; perintah `run_doc_tests.sh` yang benar kemudian lulus. Kegagalan redaksi validator selama integrasi diperbaiki pada dokumentasi, tanpa melemahkan validator.
- Review baca-saja independen memeriksa sumber, scope acceptance, 299 ID, perubahan 91/8/66, serta kesamaan entry di luar lingkup. Review awal meminta perbaikan daftar bukti eksekusi eksternal yang masih memakai ID baseline; artefak baseline dipertahankan, daftar final diganti dengan TSV valid yang tepat memuat 54 ID accepted kanonik dan 17 ID accepted §§11–20. Pemeriksaan independen lanjutan atas parity fixture/assertion/source-index memberikan **PASS**, tanpa blocker tersisa. Validator, **41 regresi**, dan doc-test dijalankan ulang setelah perbaikan dan lulus. `git diff --check` lulus. Full Linux/Windows CI pada head final **belum diklaim**, terpisah dari pengujian lokal di atas.

## ID yang berubah

Awalan setiap ID pada tabel kelompok berikut adalah `G2-PRD-`. Daftar ini adalah perubahan terhadap head awal, bukan daftar fitur baru yang diterima.

### 66 ID lama dikoreksi

- §11: `CHAR`, `FLOAT-MATERIALIZE`, `FLOAT128`, `RADIX`.
- §12: `CONTAINER-STORAGE`, `DOC-ATTACH`, `DOC-CLAW`, `DOC-ORPHAN`, `DOC-PRIVATE`, `ESCAPE`, `FSTRING`, `SHAPE-STORAGE`, `STR-OWN`, `STR-VIEW`, `STRING-CLONE`, `STRING-MOVE`, `STRING-STORAGE`, `STRING-TEMP`, `STRING-OOM`.
- §13: `CONTRACT-LOCALITY`, `DUPLICATE`, `METHOD-LOCALITY`, `OPERATOR`, `THIRD-MODULE`, `WITH`.
- §14: `GET`, `SLICE`, `BOUNDS`, `UTF8`, `BORROW`, `LEGACY-SLICE`.
- §15: `ANNOTATION-CONSTRUCTOR`, `ANNOTATION-DEPRECATED`, `ANNOTATION-TEST`, `FFI`, `REWRITE`, `CONFIG-LINK`, `CONFIG-DUPLICATE`.
- §16: `SOURCE`.
- §17: `CYCLE`, `MODULE`, `PRIVATE`, `SUPER`, `SHARE`, `NAMESPACE`, `DIRECT-ITEM`, `BASENAME-ALIAS`.
- §19: `ARENA`, `BOUNDS`, `CHECKED`, `NOALIAS`, `SATURATING`, `WRAP`, `WRAPPING`.
- §20: `CLOSURE-CAPTURE`, `CLOSURE-MOVE`, `CLOSURE-RETURN`, `DEFAULT`, `NARROW-CHAR`, `NARROW-FLOAT`, `NARROW-INT`, `NARROW-MAYBE`, `SEND`, `SHOW`, `STATIC`, `SYNC`.

### 91 ID baru

- §11: `CHAR-WIDTH`, `CHAR-UTF8`, `CHAR-ESCAPE`, `FLOAT128-LLVM`, `RADIX-BINARY`, `RADIX-OCTAL`, `DECIMAL-SEPARATOR`, `SUFFIX`, `SUFFIX-CANONICAL`, `SUFFIX-FALLBACK`, `SUFFIX-INVALID`, `LITERAL-RANGE`.
- §12: `STR-ALIAS`, `STR-LAYOUT`, `STRING-ALIAS`, `STRING-IMMUTABLE`, `STRING-MOVE-ONLY`, `STRING-LAYOUT`, `STRING-SIZE`, `STRING-ALIGN`, `LITERAL-TYPE`, `LITERAL-NOALLOC`, `LITERAL-NOCOPY`, `NO-IMPLICIT-OWNED`, `STRING-FROM`, `STRING-PARAM`, `STRING-RETURN`, `STR-NO-DESTRUCTOR`, `STR-NO-FREE`, `BORROW-INFER`, `VIEW-ESCAPE`, `VIEW-AFTER-MOVE`, `VIEW-AFTER-MUTATE`, `TEMP-NO-STORE`, `TEMP-NO-RETURN`, `TEMP-NO-JOIN`, `CONTAINER-OWNED`, `CHOICE-STRING`, `SHAPE-STRING`, `MAYBE-VIEW`, `STATIC-NODISPATCH`, `EXPLICIT-ALLOC`, `DIAG-LOCATION`, `DIAG-REASON`, `DIAG-HINT`, `DIAG-COLOR`, `DIAG-NO-COLOR`, `DIAG-WINDOWS`, `NO-RAW`, `STRING-BUILDER`, `FSTRING-MODES`.
- §13: `WITH-AND`, `WITH-OPTIONAL`, `WITH-KEYWORD`, `ORPHAN-GRAPH`.
- §14: `BYTE-INDEX`, `NO-STR-FROM`, `NO-TO-OWNED`, `LOWERING`, `SPAN-EXAMPLE`, `UTF8-CHECK`.
- §15: `FFI-CONTRACT`, `FFI-RAW`, `FFI-SAFE-WRAPPER`, `ANNOTATION-TOKEN`, `ANNOTATION-CLOSED`, `ANNOTATION-GOVERNANCE`, `ANNOTATION-CONSTRUCTOR-LOCALITY`, `REWRITE-POSTFIX`.
- §17: `FOLDER`, `NO-MODULES-FILE`, `NO-PUB`, `SRC-OTHER`, `LLVM-SYMBOL`, `LLVM-SHARE`, `LLVM-PRIVATE`, `LLVM-MERGE`.
- §18: `BOUNDS-ELISION`, `SAFE`, `SIMPLE`, `NO-BOILERPLATE`, `FAST`, `NO-HIDDEN-COST`, `SELFHOST-PREREQS`.
- §19: `DEBUG`.
- §20: `MAYBE-TYPE`, `MAYBE-SOME`, `MAYBE-NONE`, `NARROW-INT-RELEASE`, `NARROW-FLOAT-NAN`, `NARROW-FLOAT-LLVM`.

### 8 ID dihapus atau digabung

- §11: `FLOAT`.
- §12: `STR-INDEX`.
- §13: `CONTRACT`.
- §16: `RATIFY`.
- §17: `CASE`.
- §18: `V01`, `V10`.
- §20: `THREAD`.

## Residual dan status PR

Residual tetap mencakup materialisasi Float/Float128, lexer penuh; layout, clone/move/owned-param/return dan borrow graph String di luar slice accepted, OOM dan storage generik; f-string/builder/Show; generic/orphan; direct bounds/UTF-8, Span dan graf owner; FFI/TOML/annotation; LLVM modularitas/noalias; overflow dan API aritmetika eksplisit; narrowing; closure; kontrak Default/Send/Sync; negatif casing static; audit Arena; benchmark dan kualitas gerbang v1. Daftar per-atom di ledger tetap sumber rincian.

PR #27 tetap terbuka dan tidak di-merge. Tidak ada perubahan PR #22–#26, tag, version atau release. Audit dokumentasi terlingkup ini tidak menyatakan seluruh PRD exhaustive, implementasi selesai, atau G2 ditutup. **G2 NOT CLOSED; Linux/Windows CI untuk head final tetap follow-up.**
