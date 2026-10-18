# PR25 — nama fixture shorthand try dalam fungsi Unit

Fixture negatif yang sebelumnya bernama `try_return_expression.cat` diganti menjadi `try_shorthand_unit_function.cat`, beserta entri manifest. Isi fixture, pesan diagnostik, dan source span tidak berubah: kasusnya adalah `val value = try step(flag)` dalam fungsi Unit, bukan `return try ...`.

README integrasi dan matriks menjelaskan batas tersebut. Penolakan syntax `return try` memiliki fixture frontend terpisah, `revise_bad_try_return_expr.cat`, yang dijalankan untuk check dan AIR; matriks negatif integrasi ini tidak diklaim mengujinya pada empat tahap. Tidak ada syntax atau semantik baru. Jumlah fixture negatif tetap 13 dengan 52 pemeriksaan tahap.
