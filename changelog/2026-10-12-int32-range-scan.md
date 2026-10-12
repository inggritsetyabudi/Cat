# Iterasi `scan` untuk `Range[Int32]` (2026-10-12)

Slice parsial untuk PRD §7 / milestone v0.3. Kini mendukung `scan item over range` dan `scan item, i over range` hanya untuk `Range[Int32]`. §7 dan v0.3 tetap parsial: arrays, tipe integer lain, `Range[Char]`, dan slicing belum didukung. Tidak ada perubahan pada Arena.

## Kondisi awal

Range literal dan membership sudah tersedia, dan AST/parser/sema/OIR telah mengenali scan dengan indeks opsional. Namun LIR dan backend LLVM belum dapat mengiterasi `Range[Int32]`; backend iterator masih hanya menurunkan koleksi `Str`/`Span`.

## Implementasi

- OIR/LIR kini membawa instruksi inisialisasi range tersendiri sebelum header scan serta nama binding indeks opsional. AIR/OIR/LIR menampilkan nama dan tipe item/indeks.
- Sema membatasi indeks eksplisit pada `Range[Int32]`; indeks bertipe `USize` dan dimulai dari nol untuk setiap masuk ke loop.
- LLVM mengekstrak start, end, dan flag inclusive sekali dari nilai range, menyimpan state iterator, memakai perbandingan bertanda untuk batas exclusive/inclusive, serta menulis item dan indeks yang sedang diproses sebelum memasuki body.
- Setelah elemen nonfinal, nilai Int32 dan indeks USize bertambah satu. Elemen terakhir pada range inclusive ditandai selesai **sebelum** ada increment; karena itu `INT32_MAX..=INT32_MAX` (juga batas inclusive yang berakhir di `INT32_MAX`) berhenti tanpa overflow. Range kosong/terbalik tidak memasuki body.
- `stop` keluar melalui label exit scan dan `skip` kembali ke header; progres iterator untuk elemen tersebut sudah disiapkan sebelum body sehingga kedua jalur mempertahankan urutan elemen.

## Regresi

- `claw/tests/native/fixtures/revise_int32_range_scan.cat` menguji scan atas literal dan nilai `Range[Int32]` tersimpan, evaluasi endpoint ber-efek samping sekali, scan berindeks dan progres indeks, `stop`, `skip`, range kosong/terbalik, batas exclusive dekat `INT32_MAX`, serta `INT32_MAX..=INT32_MAX`.
- Frontend memeriksa sintaks/AIR kedua bentuk, tipe indeks melalui pemanggilan fungsi berparameter `USize`, dan penolakan scan berindeks atas `Str` sebelum binary native dibuat.
- Golden `claw/tests/oir/golden/int32-range-scan.oir` mengunci inisialisasi range dan binding indeks; harness native memeriksa signed inclusive comparison, increment USize `i64`, output, dan batas waktu agar regresi overflow tidak menggantung.

## Verifikasi

Clean configure/build, clean-first rebuild, dan seluruh suite (`claw/tests/run_all_tests.sh`: doc tests, frontend, OIR golden, backend, native) lulus pada salinan disposable `/tmp/cat-scan-range-final` dengan exit code 0. Native scan regression juga dijalankan dengan batas waktu dan menghasilkan output persis sesuai urutan item/indeks yang diharapkan.
