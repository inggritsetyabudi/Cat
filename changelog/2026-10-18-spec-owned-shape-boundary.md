# Batas proyeksi owned shape pada SPEC

`SPEC.md` menjelaskan bahwa shape ber-field owned `String` tetap memiliki recursive cleanup dan dapat dipindahkan secara utuh, tetapi konsumsi field owned melalui projection ditolak sampai per-field partial-move state dan selective drop scheduling tersedia. Batas ini mencakup nested/temporary projection, consuming method receiver, owned operator operand pada operator yang didukung, dan projection di body method.

Field yang diproyeksikan melalui borrowed owner tetap berupa view, bukan transfer ownership. Read/borrow, explicit `String.clone()`, plain-field reads, dan non-consuming owned-field access tetap didukung. Owned field dalam view-shape juga tidak boleh dipindahkan melalui projection.

Dokumentasi menyebutnya keterbatasan compiler, bukan larangan partial move shape oleh PRD, dan tidak mengiklankan shape destructuring. Tidak ada perubahan desain kanonis, borrowed-field exception baru, atau penandaan fitur selesai. Validasi mengikuti matriks regresi serta gerbang lokal dan CI pada PR.
