# Menolak borrowed `Str` yang lolos dari owner lokal cabang

Hasil ekspresi `if` sebelumnya dapat menyimpan `owner.as_str()` walaupun `owner: String` dibuat di dalam cabang. Lowering menyimpan view hasil cabang, membebaskan owner pada akhir cabang, lalu menggunakan view di luar cabang. Bentuk assignment sebagai statement sudah ditolak; bentuk hasil ekspresi belum memiliki pemeriksaan scope yang setara.

## Perbaikan

- Periksa asal view hasil `if` dan `pick` sebelum scope cabang ditutup. View ke owner yang akan dihancurkan ditolak dengan lokasi source dan hint untuk memindahkan owner ke luar cabang atau mengembalikan salinan owned `String`.
- Pertahankan identitas owner dan jalur pinjaman hasil join, termasuk alias, slice, ekspresi bersarang, dan carrier `Maybe[ref Str]`. View hasil join harus tetap menghalangi move atau overwrite owner.
- Bila beberapa asal pinjaman tidak dapat diwakili secara aman oleh model yang tersedia, tolak join secara konservatif. Jangan menganggap cabang literal menghapus kewajiban pinjaman pada cabang lainnya.
- Literal statis, parameter borrowed yang memiliki satu sumber return yang sah, dan hasil owned `String` tetap mengikuti kontrak yang ada. Tidak ada fallback runtime, anotasi lifetime baru, perubahan Arena, borrowed field baru, atau implementasi f-string.

## Regresi

Fixture negatif menggunakan contoh multiline `if` yang dilaporkan tanpa mengubah bentuk source. Tes juga mencakup cabang `else`, owner luar yang dipindah/ditimpa, alias dan slice, nested `if`, carrier `Maybe`, dan ekspresi `pick` dengan owner lokal maupun tiga asal cabang yang berbeda. Build untuk source yang ditolak wajib tidak menghasilkan LLVM IR atau executable.

Fixture native positif mencakup kedua kondisi parameter borrowed, literal statis, alias yang tetap meminjam owner luar, cleanup owner lokal dengan hasil literal, dan hasil owned `String`. Output normal dibandingkan dengan output ASan/UBSan/LSan. Pada Linux, ketidaktersediaan sanitizer untuk regresi ini adalah kegagalan, bukan alasan skip; Windows tetap mengikuti kemampuan runtime sanitizer toolchain UCRT64.

## Validasi

- Build Clang Release bersih pada snapshot disposable lulus, diikuti seluruh suite PRD/doc-command, frontend, OIR golden, LLVM backend, native, dan CLI/PTy.
- Regresi native string dan cabang lulus ASan/UBSan/LSan dengan pemeriksaan leak aktif; tidak ada UAF, undefined behavior, atau leak yang dilaporkan.
- Review independen read-only memeriksa provenance sebelum penutupan scope, alias, shadowing, cabang terminating, dan merge tiga cabang; tidak menemukan blocker pada patch final.
- Workflow CI tetap menjalankan suite lengkap pada Linux Clang Release dan Windows MSYS2 UCRT64 Clang Release. Hasil run untuk commit final dicatat pada PR.

Status fitur PRD tidak dipromosikan oleh perubahan ini; ini adalah perbaikan soundness untuk kontrak borrowed/owned yang sudah ada, bukan deklarasi selesainya fitur lain.
