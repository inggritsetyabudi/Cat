# Menolak konsumsi proyeksi owned shape yang belum aman

Baseline `e7789c6efedff677b81c2f883fe8498e32c30554` direproduksi dari git archive baru dengan build Linux Release. Fixture `multi_owned_first.cat` memindahkan `source.left` dari shape dengan tiga field `String` dan menghasilkan stdout `left`. ASan/UBSan/LSan pada O0 dan O1 melaporkan **11 byte bocor dalam dua alokasi**: 6 byte field `middle` dan 5 byte field `right`, dengan exit LSan 23. Drop field yang dipindahkan ada, tetapi kedua field saudaranya tidak dibebaskan. Full suite baseline fresh tidak dijalankan dan tidak diklaim lulus.

## Perubahan compiler

Konsumsi proyeksi field owned kini ditolak dengan lokasi sumber dan hint untuk meminjam field, memanggil `clone()` untuk salinan owned eksplisit, atau memindahkan shape secara utuh. Pemeriksaan berlaku juga untuk nested/chained projection, temporary, assignment, argument, return, method, dan operator receiver/operand. **Konsumsi dari owner borrowed melalui adapted view bukan direct owned projection**; fixture borrowed-owner memakai borrow/read atau explicit clone dan diklasifikasikan sebagai positive bila valid. Diagnostik khusus numeric owned-tuple projection tetap dipertahankan.

Root ownership saat ini hanya mencatat satu state per binding. Meneruskan konsumsi field ke root menandai seluruh shape `Moved`, sehingga drop field saudara ikut hilang. Perbaikan ini menutup jalur tersebut sebelum lowering; ini bukan implementasi partial ownership.

## Dasar dan batas

PRD §12 menjamin alokasi `String` dibebaskan tepat sekali dan shape owned tetap didukung dengan recursive drop. Batas partial move pada PRD §8 berada di konteks tuple; ini bukan klaim bahwa PRD secara eksplisit melarang partial move shape. Dukungan shape partial move belum dijanjikan oleh slice implementasi yang teruji. Whole-shape move, owned parameter/return, borrow/read-only field access, explicit `String.clone()`, dan recursive drop tetap dipertahankan.

Partial move shape penuh masih membutuhkan state per field/path, aturan reuse dan aliasing, merge control flow, serta selective drop untuk scope exit, return, replacement, dan unwind. Shape destructuring tidak ditambahkan.

## Validasi

Focused final meluluskan **27 negative fixtures** (24 projection/operator cases, 2 moved-use cases, dan 1 borrow-conflict case) serta **8 positive fixtures**. Negatif ditolak pada check/OIR/LLVM/build dengan tepat satu diagnostik yang dimaksud; positif melewati check/OIR/LLVM/build/run. Seluruh 8 positive LLVM outputs lulus ASan/UBSan/LSan pada O0 dan O1 di Linux. Full suite baseline fresh tidak dijalankan. CI Linux dan Windows UCRT64 tetap menjadi gerbang terpisah.
