# C@ — metadata identitas deklarasi lokal opt-in

Arah tetap **C@ — Fast, Safe, Simple**: backend milik C@ menghasilkan object native melalui LLVM C++ API, lalu LLD eksternal menghubungkannya dengan runtime target-native. Ini bukan self-hosting, penggantian pipeline produksi, atau klaim compiler mandiri penuh. PR36 tetap draft dan seluruh opsi PoC tetap default OFF. Desain dan implementasi patch ini berasal dari struktur semantik C@ sendiri; tidak mengadopsi implementasi internal bahasa lain atau menamai ulang pipeline lain.

## Yang berubah

`SemanticAnalyzer` mendapat opsi metadata lokal internal yang default-nya `false`. Hanya driver eksperimental menerima `--local-binding-keys`. Dengan opsi ini, `defineVariable` mengalokasikan token opaque pada declaration creation untuk `BindingStmt` biasa, bukan tuple pattern. Token existing `backendBindingKey` diteruskan melalui `Symbol`, resolved identifier, state/drop ownership, OIR dan LIR; tidak dibuat ulang dari nama, posisi source, label cabang atau urutan lowering. Tuple/pick/try binder tetap memakai kebijakan key lama dan belum masuk irisan ini. Scan tetap memakai key semantik existing.

Token hanya berlaku dalam satu hasil analisis. Tidak ada janji stabil lintas kompilasi, serialisasi, specialization atau cache. Analisis baru dengan analyzer baru menulis ulang metadata deklarasi/use; metadata lama bukan identitas global. Formal parameter, static symbol, synthetic SSA/result/temporary dan member identity bukan source-local declaration ID. Actual dan formal bukan deklarasi yang sama; ABI/transfer ownership lintas call tidak diubah. Opsi ini **bukan canonical identity universal**. Jika correspondence yang dibutuhkan tidak ada, backend tidak boleh menciptakannya dari nama atau lowering order.

Saat opsi tidak diberikan, alokasi key tetap shadowing-only seperti sebelumnya. Guard duplicate object di adapter tidak diubah atau dilemahkan. Tidak ada perubahan ownership/move/drop rules, Arena/self-reference, borrowed fields, runtime, ABI atau driver produksi.

## Gerbang yang dipertahankan dan ditambah

- Corpus scalar, arithmetic, String dan ABI oracle lama tidak diubah. Keenam source audit identity beserta expected bytes dan allocation counts tetap utuh.
- Gate `llvm-cpp-binding-identity-audit` tetap menguji mode lama: empat penolakan tanpa object/IR dan dua positive controls, serta produksi O0/O2 native.
- Lock implementasi historis tetap utuh. File delta terpisah mengizinkan tepat `sema.cpp`, `sema.h`, dan driver PoC, dengan hash sebelum yang harus sama dengan lock historis serta hash sesudah yang eksplisit. Semua file protected lainnya dan himpunan file tetap dikunci. Ini bukan klaim bahwa tiga file tersebut byte-identical dengan baseline.
- Gate metadata baru memeriksa metadata AST/ownership/OIR/LIR nyata, bukan hanya pretty-printer.
- Gate native baru menjalankan corpus yang sama dengan opsi lokal, tanpa compiler di PATH saat emission/link, memakai object native + runtime archive immutable + LLD + CRT/SDK target. Semua output, observer allocation/free dan runtime sanitizer yang tersedia diperiksa. Produksi O0/O2 tetap dijalankan, bukan diganti oracle sintetis.

## Validasi

Hasil final, SHA yang diuji, review independen dan status CI dicatat pada komentar PR setelah run. Changelog ini tidak menyatakan PASS sebelum hasil aktual tersedia. Windows sanitizer hanya boleh dilaporkan PASS bila probe compile/link/run mendukungnya; kegagalan availability adalah SKIP dengan diagnostic. Instrumentasi C runtime tidak berarti semua instruksi LLVM yang dihasilkan terinstrumentasi.

## Batas berikutnya

Belum ada promosi ke default atau merge ke main. Identitas formal/synthetic dan aturan provenance lintas seluruh jalur masih perlu kontrak serta bukti tersendiri sebelum klaim canonical universal. Gate kelulusan subset lokal tidak menutup keseluruhan tujuan kemandirian C@.
