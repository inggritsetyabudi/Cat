# G2 — keputusan pembagi integer nol

## Keputusan

Menggunakan otorisasi Setya pada 18 Oktober 2026 untuk keputusan teknis/desain yang menjaga sintaks dan tujuan Fast, Safe, Simple, PRD §6 kini menetapkan defect runtime tidak dapat dipulihkan untuk builtin integer `/`, `%`, `/=`, dan `%=` dengan pembagi nol pada debug/release. Ini mencakup literal dan ekspresi konstan yang sah dalam konteksnya, bukan penolakan kompilasi khusus konstanta. Cabang/short-circuit yang tidak dievaluasi tidak menimbulkan defect. Pembatasan initializer static tidak berubah.

Defect menghentikan proses dengan diagnostik stderr dan status nonzero, tanpa janji unwind/destructor cleanup; nomor exit/signal tidak dinormalkan antarplatform. Operasi float tetap IEEE-754. Signed MIN/-1 tetap tunduk pada aturan overflow lama, bukan aturan pembagi nol baru.

## Alasan dan batas

Satu aturan menghindari perbedaan tersembunyi antara konstanta/dinamis serta debug/release dan tidak menyerahkan operasi integer invalid ke LLVM. Pembacaan PRD §6/§19/§20 dan rencana perubahan tidak menemukan kebijakan pembagi nol yang bertentangan. Rencana perubahan mendapat acceptance criteria terkait tanpa mengubah milestone v0.8 atau staging target debug.

Arah ini konsisten dengan PR #23 yang diperiksa read-only, tetapi PR tersebut tetap terbuka dan tidak diubah atau dikreditkan ke main. Ledger menggunakan `[diputuskan]`, bukan `[diterima compiler]`. Tidak ada perubahan compiler, runtime, sintaks, Arena, f-string, PR #22–#26, merge, versi, tag, atau release. G2 tetap terbuka.

## Validasi

Perubahan ini berupa keputusan dokumentasi; hasil validator, doc-test, suite Release disposable, review independen, dan CI final dicatat pada PR #27. Hasil PR #23 bukan hasil validasi perubahan ini.
