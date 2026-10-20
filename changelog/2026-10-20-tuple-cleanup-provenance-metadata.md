# C@ — metadata provenance cleanup tuple

Melanjutkan PR #36 pada branch `poc/llvm-cpp-adapter-20261019` dari `15ec7579d28eb95fee27cafc7fd3952ba6254fe6`. PR tetap draft; `CLAW_ENABLE_LLVM_CPP_POC` dan `--local-binding-keys` tetap default OFF. Ini perbaikan metadata/invariant/diagnostic boundary internal, bukan aggregate lowering, promosi produksi, penutupan versi, atau perubahan keputusan PRD.

## Batas yang diperbaiki

Audit sebelumnya membuktikan deklarasi leaf tuple mempunyai tempat untuk ID sendiri, tetapi `appendDropForValue` membentuk field temporary lalu membuang hubungan root/path pada recursive drop. Menyalin ID parent ke setiap drop akan menyamakan sibling dan mengarang binder wildcard. Patch ini tidak melakukan itu.

Saat mode canonical aktif, sema memberi token opaque pada **deklarasi leaf `val` tuple yang benar-benar ada**, melalui pembuatan Symbol yang sudah dipakai resolver. Tuple pattern induk dan `_` tidak menjadi deklarasi. Mode default mempertahankan kebijakan shadowing lama, termasuk shadowing tuple leaf. Ownership tetap menjadwalkan drop whole-binding dengan key eksplisit yang sama; aturan consume/move/drop tidak diubah.

## Tiga hal yang sengaja berbeda

`SourceProjection` menyimpan key binding owner cleanup saat ini dan jalur field numerik relatif terhadap owner tersebut. `Provenance` menyimpan source projection opsional secara terpisah dari ID storage sintetis opsional. ID sintetis nonzero dibuat pada instruksi proyeksi cleanup yang benar-benar dimaterialisasi; satu ID menghubungkan hasil field dan drop-nya. Domain ID adalah fungsi OIR/LIR yang memuatnya, bukan alokasi runtime, identitas ownership semantik, atau ID stabil lintas analisis/kompilasi.

Root source hanya berasal dari `DropAction.bindingKey` yang sudah di-resolve. Recursive cleanup menambahkan indeks field, sehingga `(owner, [0,0])`, `(owner, [0,1])`, dan `(owner, [1])` tetap berbeda. Operand lookup `bindingKey` pada child/drop sintetis tetap kosong; key lookup root tetap dipertahankan. Field result, operand perantara, dan drop membawa metadata yang sama melalui OIR → LIR. Tidak ada pencocokan nama, urutan deklarasi, atau rekonstruksi ID dari nama temporary.

Ini **provenance owner cleanup saat ini**, bukan riwayat initializer, allocation, atau seluruh transfer nilai. Binding baru yang menerima nilai moved menjadi root cleanup miliknya sendiri. Wildcard tidak mempunyai deklarasi; cleanup-nya mempunyai ID sintetis tetapi source tetap unknown. Patch ini tidak mengklaim sumber historis wildcard, provenance seluruh expression/proyeksi, atau seluruh binder pick/try. Metadata hanya ditambahkan pada jalur cleanup tuple/proyeksi yang dicakup, bukan pada semua field instruction.

## Invariant dan adapter

Pemeriksaan struktural menolak source key kosong dan ID sintetis nol. Ini bukan bukti ownership, batas path, correspondence storage, atau ABI aggregate yang valid. Tes metadata memeriksa correspondence deklarasi/use/OwnershipResult, perbedaan path sibling, reset root setelah move, field/drop ID yang persis sama dan unik dalam fungsi, wildcard unknown, serta preservation OIR/LIR.

Adapter menolak setiap provenance proyeksi nonempty, termasuk jika seseorang menempelkannya pada operand skalar atau drop `String` yang biasanya didukung. Metadata malformed mempunyai diagnostik tersendiri. `LirFieldInst`, Tuple, dan Anchor tetap unsupported; tidak ada storage lookup aggregate baru, object/IR tuple yang diterbitkan, ataupun klaim dukungan native tuple pada adapter. Direct String path yang sudah didukung tidak menerima metadata baru ini.

## Bukti dan hal yang tidak berubah

Test CTest terpisah `llvm-cpp-tuple-cleanup-provenance` memakai AST/sema/ownership/OIR/LIR program Cat nyata; semua pointer AST dipakai hanya selama Project dan SemanticAnalyzer hidup. Tes negatif adapter memeriksa metadata valid-tetapi-unsupported, source unknown, malformed, dan tidak terbitnya object/IR. Fixture mencakup plain fields, tuple nested, sibling scopes, shadowing, wildcard, dan perpindahan owner. Review independen menemukan kekurangan walker fixture; walker expression/branch diperbaiki sebelum penerimaan tes.

Lock implementasi historis serta delta local/formal sebelumnya tidak ditulis ulang. `tuple-cleanup-reviewed-deltas.json` memperpanjang rantai dari head awal dengan himpunan file berubah yang eksplisit dan tepat satu header baru. Audit tetap memverifikasi hash sebelum/sesudah, himpunan file protected, runtime, corpus, serta expected bytes/allocation count lama. Ini delta implementasi metadata yang terukur, bukan klaim bahwa source implementation byte-identical.

Gerbang penerimaan tetap Release dan host ASan/UBSan CTest, kedua suite produksi penuh, perbandingan 90+8 fixture lama × `check`/`oir`/`lir`/`llvm`, build/run Cat asli serta penolakan adapter, review independen, dan CI Linux/Windows UCRT64 pada exact head yang sama. Hasil aktual dan SHA final dicatat di komentar PR setelah run; keberadaan gerbang ini bukan PASS. Probe sanitizer Windows yang tidak tersedia tetap **SKIP**, bukan PASS. Instrumentasi runtime C bukan bukti semua instruksi LLVM yang dihasilkan terinstrumentasi.

Sintaks, semantik, ABI/runtime produksi, formatter output, driver default, PRD/marker versi, Arena self-reference dan borrowed fields tidak diubah. Pick/try, broad CFG semantics dan aggregate lowering tidak dikerjakan. Blocker berikutnya tetap kontrak serta implementasi storage/aggregate ABI Tuple/Anchor pada adapter; metadata cleanup ini sendiri tidak menyelesaikannya dan tidak mengizinkan promosi default atau merge.
