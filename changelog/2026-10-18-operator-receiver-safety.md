# Safety ownership pada receiver operator overload

Lowering operator biner memanggil method `add`, `sub`, `mul`, `div`, `equal`, atau `compare` dengan operand kiri sebagai receiver dan operand kanan sebagai argumen pertama. Ownership checker kini menerapkan `FunctionSignature` yang sama seperti sema dan OIR: receiver/parameter owned dikonsumsi, sedangkan view dipinjam. Borrow receiver tetap aktif selama operand kanan diperiksa, sehingga operand kanan tidak dapat memindahkan owner yang masih dibutuhkan receiver.

Ini menutup jalur konsumsi field owned melalui operator, misalnya `outer.inner + rhs` dan `lhs + outer.inner`, menggunakan diagnostik partial-move yang sama dengan projection langsung. Transfer whole binding dan operator borrowed tetap dibedakan; partial move shape tidak diimplementasikan. Perubahan tidak mengubah operasi aritmetika, semantik division, kontrak Arena, atau larangan borrowed field pada shape normal.

Regresi permanen pada `claw/tests/native/owned_shape/` memisahkan proyeksi receiver dari proyeksi argumen agar masing-masing penolakan tidak tertutup diagnostik lain. Program positif memeriksa transfer whole shape dan operator borrowed, sedangkan kasus konflik memeriksa borrow receiver yang masih aktif. Hasil final dicatat bersama gerbang pengujian lokal dan CI; inspeksi sumber saja bukan bukti lulus.
