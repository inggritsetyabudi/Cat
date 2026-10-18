# G2 — inventaris proposal Roadmap dari sumber asli

Inventaris bertambah **108 atom proposal**, tanpa menambah atau mengubah aturan di Roadmap. Ledger, source index, dan manifest kini berisi **473 ID: 54 diterima compiler, 295 diputuskan, 124 diusulkan**. Keenam belas atom rewrite lama dan seluruh label sebelumnya dipertahankan. Koreksi output PICK dicatat dalam [changelog tersendiri](2026-10-18-g2-pick-output-evidence-correction.md).

[Header Roadmap](../cat-northstar/ROADMAP-v1-plus.md) berbunyi: «Semua isi dokumen ini berstatus **usulan desain**. Belum ada yang diputuskan kecuali: `rewrite` (macro) ada di v1.x, dan self-hosting dimulai setelah v1 terpenuhi.» Penempatan versi tertentu dan detail desain berikut tidak menjadi ratifikasi atau penerimaan compiler. Keputusan staging rewrite tetap `G2-PRD-15-REWRITE`; detail §4 tetap proposal. Self-hosting setelah v1 tidak diubah menjadi klaim implementasi atau ratifikasi target v1.2.

## Atom yang ditambahkan

Setiap atom memiliki ID `G2-ROADMAP-<bagian>-<irisan>`, sumber `ROADMAP:<bagian>`, tautan heading, dan kutipan persis sumber asli pada ledger dan source index. Manifest proposal tidak memiliki fixture/assertion acceptance. Jumlah per bagian:

- **§0: 5 atom.** Desain tertulis, non-goals, tes negatif, kriteria Fast/Safe/Simple terukur, dan bootstrap biner identik. Kebijakan edition tidak diduplikasi.
- **§1: 8 atom.** Urutan versi v1.1–v2.0, terpisah dari semantik fitur. Usulan rewrite v1.3 tidak menggantikan keputusan v1.x.
- **§2: 13 atom.** Delapan komponen cakupan pustaka; fmt satu gaya tanpa opsi; doc dari `///` item `share`; test dari `@test`; language-server inlay hint parameter; kecukupan pustaka untuk menulis compiler. Cakupan pustaka pasca-v1 bukan duplikasi acceptance kecukupan empat komponen gerbang v1.
- **§3: 8 atom.** stage0/1/2/3, kesamaan byte stage3 dengan stage2, suite dan hasil sama di stage0/1/2, penanganan temuan bahasa, retensi compiler C++ selama fixed point beberapa rilis.
- **§5: 22 atom.** Tujuan bebas data race tanpa biaya runtime tambahan; derivasi struktural; arti Send/Sync; spawn dan scoped borrow; eksklusivitas lintas thread dan perluasan dataflow; static Sync; defect anak; runtime/LLVM atomik; seq_cst default; ordering lemah eksplisit/terdokumentasi; closure untuk spawn dan batas capture; tiga kategori negatif; TSan dan schedule exploration.
- **§6: 8 atom.** T: Send, move setelah kirim, akhir receiver saat semua pengirim selesai, batas pencegahan deadlock, kapasitas, try_send, try_recv, dokumentasi pola aman. Tidak menciptakan signature atau hasil API baru.
- **§7: 16 atom.** core/std, triple LLVM, bare tanpa thread runtime, raw register, interrupt handler; staging Arena resmi, fixed pool, read-only, tipe polos, alloc ref Arena, reset saat borrow, Maybe saat penuh, writable tertunda; std membungkus core; tes emulator. **Arena tetap eksperimental/tertunda, tanpa implementasi.**
- **§8: 6 atom.** A thread/channel/polling, B async/await mesin keadaan, C green threads, rekomendasi A, gerbang benchmark, keputusan tertunda sampai ada data. Rekomendasi bukan keputusan; B tidak diubah menjadi event loop.
- **§9: 12 atom.** Model formal, differential testing, gerbang korpus acak; ref mut ber-scope, eksklusivitas/tes negatif; SIMD LLVM/benchmark; backend tambahan/paritas suite; GPU eksplorasi hanya berdasarkan kebutuhan proyek AI.
- **§10: 10 atom.** Kondisi deferred yang belum dimiliki owner lain: desain/negatif Convert, daftar tipe/generic Default, gerbang open shape, kebutuhan const generic fungsi, mesin macro untuk annotation pengguna, konsistensi Queue, tes tabel narrowing, Arena AST berdasarkan waktu kompilasi.

Kalimat penjelas §3 L53, «Jika stage2 dan stage3 identik (*fixed point*), compiler terbukti konsisten dengan dirinya sendiri.», dirujuk secara eksplisit oleh owner `G2-ROADMAP-3-BYTE-IDENTICAL` dan kutipan sumbernya. Ini konsekuensi pemeriksaan fixed point yang sama, bukan requirement mandiri atau bukti compiler bebas bug. Tidak dibuat atom kedua untuk hasil uji yang sama.

## Overlap: satu owner, bukan keputusan ganda

**Edition.** [§0 L15](../cat-northstar/ROADMAP-v1-plus.md#0-aturan-main-untuk-setiap-fitur-pasca-v1) menyebut breaking change hanya melalui edition dan «Kode lama tetap dikompilasi dengan aturan lamanya.» Pemilik kebijakan tetap **`G2-PLAN-EDITION-STABILITY`**, bersumber [PRD-CHANGES S114](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#5-gerbang-v10). Tidak ada atom edition proposal baru; contoh konfigurasi tidak dinyatakan sebagai API diterima.

**Send/Sync dan closure.** `G2-PRD-20-SEND` dan `G2-PRD-20-SYNC` hanya memiliki keputusan nama. Arti transfer/sharing dan derivasi struktural [§5](../cat-northstar/ROADMAP-v1-plus.md#5-v14-threading) belum diratifikasi sehingga mempunyai atom proposal sendiri. `G2-PRD-20-CLOSURE-CAPTURE`, `G2-PRD-20-CLOSURE-MOVE`, dan `G2-PRD-20-CLOSURE-RETURN` tetap pemilik semantik dasar. Dokumentasi bentuk tetap `G2-PLAN-CLOSURE-SECTION`, `G2-PLAN-CLOSURE-SHORT-DOC`, `G2-PLAN-CLOSURE-BLOCK-DOC`, `G2-PLAN-CLOSURE-FN-TYPE-DOC`; tes negatif tetap `G2-PLAN-CLOSURE-BORROW-NEGATIVE`. Proposal baru hanya konteks spawn, kewajiban move capture milik untuk spawn, dan seluruh capture Send; tidak melabel ulang closure dasar.

**Borrow dan channel.** Liveness dan konflik umum tetap `G2-PRD-9-DATAFLOW`, `G2-PRD-9-MUTABLE-OVERLAP`, `G2-PRD-9-OWNER-MOVE`. Perluasan lintas spawn dan move setelah kirim adalah scope baru, bukan bukti implementasi borrow checker umum. Larangan `ref mut[s]` v1 tetap `G2-PRD-9-REFMUT`; usulan ber-scope pasca-v1 bukan pencabutan larangan itu.

**Pustaka/tooling.** `G2-PLAN-M10-SELFHOST-FILE-IO`, `G2-PLAN-M10-SELFHOST-STRING-BUILDER`, `G2-PLAN-M10-SELFHOST-VEC`, `G2-PLAN-M10-SELFHOST-MAP` dan agregator `G2-PRD-18-SELFHOST-PREREQS` tetap pemilik acceptance kecukupan gerbang v1. Literal tetap `G2-PRD-2-VEC`, `G2-PRD-2-MAP`, `G2-PRD-2-SET`; string builder `G2-PRD-12-STRING-BUILDER`. Nama/varian Maybe/Result, annotation test, dan share tidak diputuskan ulang oleh usulan helper, test discovery, atau generator doc.

**Arena.** `G2-PRD-19-ARENA` tetap pemilik audit tertunda; `G2-PLAN-ARENA-EXPERIMENTAL` dan `G2-PLAN-ARENA-V1-EXCLUSION` tetap pemilik label/pengecualian v1. [§10 L216](../cat-northstar/ROADMAP-v1-plus.md#10-daftar-keputusan-tertunda) mengulangi fixed pool/read-only, alloc ref Arena, tipe polos, dan target resmi v1.6 dari §7; semuanya dirujuk ke atom §7, bukan diduplikasi. Arena AST L217 berbeda: pilihan sementara Anchor atau vec+indeks dan data waktu kompilasi mempunyai owner baru `G2-ROADMAP-10-ARENA-COMPILER-AST`.

## Keputusan tertunda §10

- **Convert (L207):** [PRD §5 L280](../cat-northstar/PRD.md#5-try-dan-pola-error) menyatakan tidak ada konversi diam-diam dan Convert tertunda; error type identik tetap `G2-PRD-5-ERROR-IDENTITY`. Dua owner proposal baru mencatat desain tertulis dan tes negatif kontrak Convert dengan target v1.x, bukan melonggarkan identitas error.
- **Default (L208):** nama tetap `G2-PRD-20-DEFAULT`, API tertunda `G2-PRD-10-DEFAULT-API`, larangan baca sebelum init tetap `G2-PRD-10-READ`. Tidak ada nol otomatis. Dua atom baru hanya daftar tipe/perilaku generic; target v1.x bukan acceptance.
- **open shape (L209):** `G2-ROADMAP-10-OPEN-SHAPE-GATE` mencatat belum ada dan hanya dipertimbangkan bila lint/inlay tidak cukup. Locality/konstruksi tetap `G2-PRD-1-SHAPE` dan `G2-PRD-1-NEW`, tanpa pelonggaran.
- **fn f[N] (L210):** owner baru khusus const generic fungsi dan kebutuhan nyata pustaka; sementara memakai Span[T]. `G2-PRD-2-CONST-N` hanya Array/Span, bukan dukungan const generic fungsi.
- **Closure (L211):** keberadaan, bentuk, capture dan spawn dirujuk ke owner PRD/PLAN serta §5 di atas; tidak ada ID deferred closure baru.
- **Annotation pengguna (L212):** bawaan tetap `G2-PRD-15-ANNOTATION-CLOSED`/`G2-PRD-15-ANNOTATION-GOVERNANCE`; mesin bersama tetap `G2-ROADMAP-4-REWRITE-USER-ANNOTATION-ENGINE`. Atom §10 hanya kondisi mesin macro selesai dan target usulan v1.3.
- **Nama kontrak thread (L213):** Send/Sync tetap owner PRD:20, tanpa atom penamaan baru. Catatan nama final nanti di §5 L105 tidak mengalahkan keputusan PRD.
- **Queue (L214):** `G2-PRD-2-QUEUE` tetap pemilik Queue.new sampai literal ditentukan. Atom §10 hanya target v1.x/konsistensi vec/set/map.
- **Narrowing (L215):** debug defect, release truncate dan konversi aman tetap `G2-PRD-20-NARROW-INT`, `G2-PRD-20-NARROW-INT-RELEASE`, `G2-PRD-20-NARROW-MAYBE` beserta atom konversi lain. Atom baru hanya tes tabel, bukan acceptance atau perubahan keputusan v0.5.

## Validasi dan sisa

`feature_status_roadmap.py` menginventarisasi irisan sumber secara eksplisit, independen dari manifest. Pada korpus PRD penuh, validator menolak atom wajib yang dihapus serentak dari ledger/index/manifest, perubahan label/scope, dan frasa sumber yang hilang. Source index memeriksa kutipan verbatim dalam heading; manifest memeriksa kesamaan ID/anchor. Regresi menegaskan proposal baru tidak memiliki fixture/assertion acceptance dan hitungan rekonsiliasi.

Roadmap, PRD, PRD-CHANGES, compiler, dan sintaks tidak diubah. Tidak ada implementasi Arena, f-string, threading, channel atau async; tidak ada kredit PR22–PR26, merge, versi, tag atau release. **G2 tetap NOT CLOSED.** Inventaris ini tidak mengklaim exhaustiveness universal atau seluruh bukti acceptance memadai; ratifikasi detail usulan dan residual implementasi ledger tetap terbuka.
