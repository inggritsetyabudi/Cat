# PoC backend: pin exact LLVM 23.1.3

Kelanjutan PoC terisolasi pada PR36. SDK LLVM18.1.3 pada eksperimen awal diganti **exact LLVM23.1.3**; jalur produksi tidak dipindahkan. `CLAW_ENABLE_LLVM_CPP_POC` tetap default OFF. Tidak ada perubahan PRD, penanda penerimaan bahasa, tag/version/release, atau fallback ke backend lama.

## Integrasi yang dibuktikan

CMake configure/build/link memakai `llvm-config` SDK resmi23.1.3 dan absolute static component archives untuk `core codegen mc target x86codegen x86desc x86info` beserta closure dependency/system libraries. Tidak mengandalkan target monolithic `LLVM` atau compile overlay terpisah. Pin diperiksa pada versi executable SDK, header adapter saat compile, dan output versi dari library LLVM yang benar-benar ditautkan. Versi18.1.3,23.1.4,23.1.30 ditolak; konfigurasi OFF tidak membutuhkan SDK meskipun path SDK sengaja tidak valid.

Adapter memakai `llvm::Triple` bertipe untuk target lookup/TargetMachine/module serta `getTerminatorOrNull()` untuk blok yang belum terminated. Compile mode RTTI adapter mengikuti SDK; exceptions tetap aktif. Library SDK/header/flags hanya menjadi dependency privat adapter. SDK Linux resmi adalah static/no-RTTI; archive SDK dipin SHA256 `14d2f701eb68fb799001bdea6231048f3990690fa2f406d563555ff8f744daba`, dengan helper ekstraksi selektif yang memeriksa hash/ukuran dan tidak menghapus destination nonempty yang belum terverifikasi.

Pada Linux x86_64: configure dan semua target build berhasil; 4/4 gerbang CTest lulus (linked SDK version, configure/pin contract, adapter valid/malformed, differential end-to-end41kasus). Object diverifikasi ELF64 little-endian ET_REL mesin62, IR triple `x86_64-unknown-linux-gnu`, DataLayout dicatat dan diperiksa64-bit. Link terpisah menghasilkan executable native yang dijalankan dan dibandingkan exit/stdout/stderr dengan compiler produksi. Empty-PATH emission tetap lulus. Semua16suite produksi lama lulus tanpa mengubah script/oracle.

Binary produksi `claw` dengan PoC ON identik byte-for-byte dengan build default sebelumnya: SHA256 `a046fa2bd015984b2746d53f88cd0e75378b9b2c55537e39430b5e6a8f17a857`. `ldd` tidak menunjukkan LLVM/Clang sebagai dependency binary produksi; LLVM tertaut statik hanya pada executable eksperimen.

## Gate dan batas

Workflow baru khusus PoC mengaktifkan flag ON untuk build Linux release dan ASan+UBSan, mengunggah provenance versi/closure, hasil CTest, dan log semua suite produksi. Workflow produksi, PTY Windows, serta baseline-vs-fixed Windows lama tidak diubah. Kegagalan baseline lama O0 yang menghasilkan `0xC0000005` bukan `0xC00000FD` tetap harus dilaporkan, bukan disembunyikan dengan perubahan oracle.

Bukti lokal ini hanya Linux x86_64. Ketersediaan SDK MSYS2 UCRT23.1.3 bukan bukti adapter Windows: perlu gate native PoC ON yang membuktikan COFF, triple/DataLayout, link object terpisah, dan eksekusi `.exe` sebelum cakupan diperluas. Suite CI/sanitizer, review exact-SHA, serta benchmark terkontrol dilaporkan pada bukti pendamping; tidak ada klaim migrasi, kecepatan universal, atau kesiapan produksi.

Instruksi dan batas subset: [README PoC](../claw/experimental/llvm_cpp/README.md). Semua batas semantik eksperimen awal tetap berlaku.

## Gate native Windows x64 UCRT / COFF

Setelah milestone Linux di atas hijau, ditambahkan konfigurasi Windows terbatas pada host/compiler native x64 MinGW UCRT, SDK23.1.3 Windows GNU, dan static component libraries. Guard `_UCRT`/arsitektur/CRT, target COFF, serta DataLayout64-bit eksplisit. Tidak mengaktifkan PoC pada build produksi default; tidak mengubah ABI/semantik subset.

Job PoC ON Windows tersendiri memasang SDK MSYS2 UCRT23.1.3-1 dengan SHA256 `d23bf19409cbafe837b9ef04b29943e81955eda2842e866ac0c77ef3126be569`, memakai native UCRT Python untuk CTest, serta menyimpan provenance paket, versi library tertaut, dan closure komponen statik. Harness memeriksa object relocatable AMD64 COFF, triple `x86_64-w64-windows-gnu`, DataLayout, link object terpisah, PE32+ x64, serta eksekusi native dan exit/stdout/stderr. Tes isolasi PATH menyalin hanya emitter dan DLL non-sistem yang diaudit, tanpa Clang/linker. Fixture/oracle yang lama tetap sama; sample object/IR/executable dan command aktual dipertahankan.

Ini catatan implementasi guard/gate, bukan pengganti hasil pelaksanaan: verdict Windows harus mengikuti hasil job PoC ON pada exact SHA yang ditinjau. Hasil CI produksi OFF sebelumnya tidak boleh digunakan sebagai bukti adapter. MSVC, MSVCRT, ARM64, cross-compilation, runtime/ownership ABI, dan FFI tetap di luar cakupan.
