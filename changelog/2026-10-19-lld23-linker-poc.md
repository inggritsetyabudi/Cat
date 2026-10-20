# 2026-10-19 — PoC linker LLD exact23.1.3 terisolasi

Menambahkan `CLAW_ENABLE_LLD_POC`, default **OFF** dan hanya boleh ON bersama backend LLVM C++ PoC. Tidak ada perubahan pipeline produksi, semantik PRD, marker fitur, tag, versi atau release.

Worker eksperimental `claw-lld-link-poc` menautkan `lldELF/lldCOFF/lldMinGW/lldCommon` dan closure komponen LLVM statik dari SDK23.1.3 yang sama, lalu memanggil `lld::lldMain`. Pembanding tambahan menjalankan executable resmi `ld.lld`, bukan Clang/`cc` driver. Versi exact dan input SDK diperiksa; tidak ada fallback linker/compiler. Dependency Polly yang ditarik closure LLVM LTO menggunakan arsip resmi, bukan stub simbol.

Empat tes asli tetap tidak diubah, termasuk41 kasus (15 differential,16 rejection,9 I/O,1 isolated object emission). Empat gerbang baru menguji versi library LLD, serial success/error/success re-entry, lima konfigurasi OFF/pin/fail-closed, serta native link/run dari15 object yang sama per mode. Ada proof path literal, malformed object, CRT/library hilang, trace/map, simbol/imports, output ELF/PE, runtime exit/stdout/stderr dan sepuluh pengukuran warm-link kecil yang outputnya diverifikasi. Linux menggunakan PATH kosong; Windows mengisolasi executable bersama closure DLL non-sistem agar direktori Clang tidak ikut tersedia.

CMake configure/build/link SDK resmi dan8/8 gerbang telah dijalankan pada Linux x86_64 saat implementasi. Gate CI khusus menuntut pengulangan exact-head pada Linux release/sanitizer dan native Windows x64 UCRT dengan PoC ON. Ketersediaan SDK/konfigurasi Windows bukan klaim kelulusan native; status target yang boleh dilaporkan mengikuti hasil CI dan review exact SHA, bukan CI produksi lama dengan PoC OFF.

Profil link dibatasi native Ubuntu24/GCC13/glibc ELF dan native MinGW UCRT x64 GNU-flavor COFF/PE console. CRT/startup objects, GCC support, OS loader/runtime dan import libraries tetap diperlukan. Tidak mengklaim runtime Cat, ABI seluruh bahasa, MSVC, MSVCRT, Apple, ARM64 atau cross-compilation. API worker terpisah tidak menjamin aman untuk object berbahaya, concurrent calls atau embedding ke dalam proses compiler produksi.

Suite/oracle produksi dan diagnostik baseline Windows tidak dilonggarkan. Bila baseline lama menghasilkan `0xC0000005` ketika oracle menuntut stack-overflow, kegagalan tersebut tetap harus dilaporkan terpisah. Lihat [desain, sumber resmi, perintah dan batas bukti linker](../claw/experimental/llvm_cpp/lld/README.md).
