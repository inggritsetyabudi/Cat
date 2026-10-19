# Windows native Clang: pertahankan path ber-spasi sebagai satu argumen

## Masalah dan baseline

Baseline main `8a686092f1bad5ae2dda9db18d944eedcdb6ad24` gagal pada Windows UCRT64 dalam [CI 39181674147](https://github.com/inggritsetyabudi/Cat/actions/runs/39181674147). Sesudah 13 kasus komentar negatif × 13 mode lulus, build program valid gagal karena Clang menerima `.../valid` dan `comments/build.ll` sebagai dua argumen, bukan path `.../valid comments/build.ll`. Output `comments/build.exe` juga terpecah. Job Linux Release dan Linux ASan/UBSan/LSan pada baseline tersebut lulus; seluruh suite baseline lokal Linux juga lulus.

Riwayat `claw/src/driver/native_build.cpp` menunjukkan ini bug lama, bukan regresi lexer: `5438c39` sudah meneruskan path mentah ke `_wspawnvp`; pemisahan POSIX pada `95e8fdc` dan perbaikan peluncuran POSIX pada `31b8106` tidak memperbaiki quoting cabang Windows. Changelog peluncuran linker POSIX sebelumnya secara eksplisit menyebut cabang Windows tidak berubah.

## Perubahan

Hanya tiga argumen path Windows—LLVM IR, runtime C, dan output executable—diserialisasikan untuk parser CRT sebelum `_wspawnvp`. Helper mempertahankan string wide, mengutip satu argumen, meng-escape quote literal, menggandakan backslash sebelum quote, dan menggandakan trailing backslash sebelum quote penutup. Program tetap dicari sebagai `clang` di PATH; argv[0], flag `-O2 -std=c11`, callback, penantian proses, dan pelaporan kegagalan tidak berubah. Tidak ada shell baru atau perubahan cabang POSIX.

Aturan ini mengikuti dokumentasi Microsoft tentang [spawn dan argumen ber-spasi](https://learn.microsoft.com/en-us/cpp/c-runtime-library/spawn-wspawn-functions?view=msvc-170) serta [parsing argumen CRT](https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments?view=msvc-170).

## Regresi permanen

- `run_native_path_tests.py` membangun dan menjalankan program `.cat` dengan input/output/compiler/runtime ber-spasi, compiler bundle terpisah, cwd tidak terkait, nama output literal `& marker; $ value.exe`, kedua alias native build dengan output default, dan kontrol tanpa spasi. Output harus tepat `native-path-ok\n`, exit harus `7`, dan `.exe`/`.ll` harus tersedia. Artefak dipertahankan di direktori native artifacts.
- `windows_argv_tests.cpp` memeriksa 12 hasil serialisasi persis untuk string kosong, spasi, tab, path Windows, backslash akhir, quote literal, dan karakter shell. Pada Windows probe meluncurkan dirinya dengan `_wspawnvp` dan memeriksa jumlah serta isi argv yang diterima CRT. Pada non-Windows hanya round-trip proses CRT yang dilewati secara eksplisit; 12 pemeriksaan serialisasi tetap berjalan.
- Kedua runner dipasang pada `run_all_tests.sh`, sehingga gerbang Linux Release, Linux sanitizer, dan Windows UCRT64 yang sudah ada menjalankannya. Tidak ada tes lexer, spasi path, assertion, atau gerbang lama yang dihapus/dilonggarkan.

## Validasi saat perubahan disiapkan

Build baseline Linux bersih, seluruh suite baseline Linux, regresi komentar, regresi native path, 12 kasus serializer, serta `git diff --check` lulus. Round-trip proses CRT tidak tersedia lokal karena sandbox Linux; bukti Windows harus berasal dari CI pada head perubahan ini, bukan disimpulkan dari Linux. CI head dan review independen dilaporkan pada PR setelah eksekusi.

Tidak ada perubahan semantik bahasa, status penerimaan PRD, versi, tag, atau release. PR #28 ternyata sudah merged menjadi baseline di atas sebelum workstream ini dimulai; perubahan lexer tidak ditambahkan ke diff workstream driver ini. Tidak ada merge dilakukan oleh workstream ini.
