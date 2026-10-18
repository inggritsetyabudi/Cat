# Menjaga tipe nilai inline pada argumen panggilan

Pada baseline `main` `e7789c6`, `println(if true { 1 } else { 0 })` lolos `claw check`, tetapi build native gagal karena LLVM berisi `declare void @"claw.runtime.println.void"(void)`. Reproducer lengkap menunjukkan masalah yang sama pada `println(if a > b { a + b } else { 0 })`, sedangkan binding hasil `if` ke `val` sebelum `println` berhasil. Ini mismatch sema/backend pada surface yang sudah didukung, bukan fitur baru.

## Akar masalah dan perbaikan

- Parameter prelude `print`/`println` sengaja bertipe `Unknown`. Sema sebelumnya memperlakukan pointer expected-type ini sebagai konteks konkret. Unifikasi cabang kemudian mengganti tipe hasil `if`/`pick` dengan `Unknown`; OIR kehilangan storage nilai dan backend memilih runtime `void`.
- Normalisasi expected-type `Unknown` menjadi tanpa konteks pada pintu masuk analisis ekspresi. Tipe parameter konkret tetap memandu literal dan cabang; bentuk inline tidak membutuhkan binding perantara.
- Selesaikan default inference literal numerik argumen builtin, termasuk pemeriksaan rentang. Teruskan tipe join yang sudah terinferensi ke tail literal float seperti literal integer. Ini menutup pseudo-type `float literal` yang sebelumnya mencapai lowering, tanpa mengubah parser numerik atau algoritme pembulatan Float32/Float64.
- Tandai builtin pada signature dan symbol agar validasi domain runtime hanya berlaku pada prelude yang benar. Variabel lokal yang menutup nama fungsi tidak boleh memulihkan signature fungsi tersembunyi; panggilan demikian ditolak sebagai non-callable. Method bernama `println` dengan parameter tuple tetap merupakan method biasa.
- Builtin cetak membutuhkan tepat satu argumen. Domain ABI scalar, literal default, borrowed scalar, string/slice, buffer, dan pointer-handle yang sudah tersedia dipertahankan. `Unit`, tuple, array, range, shape, choice, dan `Float128` ditolak secara semantik daripada menghasilkan LLVM tidak valid atau simbol runtime tanpa definisi. `Never` tetap dapat menghentikan evaluasi sebelum call.

## Lokasi source dan perilaku error

Reproducer minimal menempatkan token `if` di baris 2 kolom 13; reproducer lengkap yang dilaporkan menempatkannya di baris 54 kolom 13. Kasus valid kini lolos check, LLVM verifier, build, dan eksekusi native dengan nilai yang benar. Tidak ada diagnostic baru untuk source valid.

Cabang tidak kompatibel, `if` nilai tanpa `else`, argumen cetak unsupported, aritas salah, dan nama builtin yang ditutup variabel ditolak sebelum lowering. Penolakan tipe menunjuk expression argumen, bukan error Clang belakangan; misalnya `Builtin 'println' cannot print value of type Unit.` pada token `if` di `2:13`. Arity menunjuk callee di `2:5`. OIR/LLVM/check tidak mengeluarkan stdout sukses dan build yang ditolak tidak membuat `.ll` atau executable.

## Regresi terintegrasi

- Fixture native yang sama dijalankan oleh frontend, backend LLVM, dan native runner: argumen builtin dan fungsi user; inline `if` dan `pick` langsung, bukan hanya fungsi pembungkus; nested if/pick; kedua varian choice; parameter `UInt8` termasuk literal tanpa suffix; aggregate tuple/array ke fungsi bertipe; evaluasi side effect sekali; cabang `Never` tunggal/seluruhnya; owned `String` borrowed dan consumed di kedua cabang; literal integer/float dan scalar ref; method bernama `println`.
- Backend memverifikasi LLVM dan menolak runtime `println.void`. Native memeriksa stdout exact, lalu relink LLVM dengan ASan/UBSan/LSan dan leak detection; stdout sanitized harus identik. Runtime sanitizer yang tidak tersedia di Linux adalah kegagalan; Windows melaporkan skip sanitizer secara eksplisit jika toolchain tidak menyediakan runtime-nya.
- Matriks penolakan menjalankan 15 kasus melalui `check`, OIR, LLVM, dan build, termasuk diagnostic, lokasi source, stdout kosong, dan larangan artefak. Fixture borrow-escape `Maybe[Str]` tetap menuntut diagnostic owner yang dihancurkan pada akhir cabang; consumer akhir diganti dari cetak aggregate unsupported ke `pick` dengan cetak payload yang sah, bukan melemahkan assertion ownership.

## Validasi dan batas klaim

Gerbang final lulus pada snapshot disposable terpisah: clean Clang Release dan compiler host ASan/UBSan dengan leak detection, masing-masing menjalankan seluruh suite doc-test, documentation CLI, frontend, OIR golden, LLVM backend, native, dan CLI status/PTy. Matriks penolakan lulus 15 kasus × empat command (60 pemeriksaan). Reproducer lengkap juga dibandingkan dengan output kontrol binding-perantara; output native dan sanitizer identik. Review independen final tidak menemukan blocker konkret. Hasil Linux dan Windows MSYS2 UCRT64 CI dicatat pada PR untuk commit final; kegagalan kandidat awal tidak dihitung sebagai hasil final.

Perubahan ini tidak menyatakan v0.1–v0.3 bebas bug atau siap rilis. PR #22 (owned-shape projections), #23 (division/remainder edges), dan #24 (target-direct float literals) tetap terpisah dan belum digabungkan. Matriks lintas seluruh fitur, property-based exhaustiveness dengan oracle, cakupan proposal PRD, serta aggregate/collection/slicing yang masih staged belum dibuktikan lengkap. Tidak ada perubahan versi/tag, merge, implementasi v0.4/f-string, partial moves, atau perubahan implementasi Arena.
