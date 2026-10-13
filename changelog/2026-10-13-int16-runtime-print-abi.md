# Perbaikan ABI printer runtime `Int16`

Probe Cat native sebelum edit mencetak `-32768_Int16` sebagai `32768` dan `-1_Int16` sebagai `65535`, sementara nol, satu, dan maksimum tampak benar. Kode runtime C sudah menerima `int16_t` dan mencetak dengan format signed `%d`; masalah berada di deklarasi/pemanggilan runtime LLVM yang tidak menyatakan ekstensi tanda untuk parameter `i16`.

Emitter kini memakai `i16 signext` untuk argumen `Int16` pada deklarasi dan callsite printer runtime—jalur ABI minimum yang diperlukan oleh temuan tersebut. Jalur printer lain dan implementasi runtime C tidak diubah. Fixture native memeriksa byte output exact untuk minimum, nilai negatif, nol, positif, dan maksimum: `-32768`, `-1`, `0`, `1`, `32767`.

Validasi lulus: `claw/tests/backend/run_backend_tests.sh` memeriksa `signext` pada deklarasi dan callsite LLVM; `claw/tests/native/run_native_tests.sh` membandingkan output exact. Program printer juga lulus saat dikompilasi dan dijalankan dengan ASan, UBSan, dan LSan.
