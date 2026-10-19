# PoC terisolasi backend LLVM C++ API

PoC pertama membuktikan jalur **LIR Cat → LLVM C++ API → native object**, dengan linking terpisah. Baseline `main`: `74fab2e39e301a25b4a6ff97fa7d84f898e6fce1`. Ini eksperimen backend, bukan perubahan semantik atau penggantian jalur produksi.

## Yang diterapkan

`CLAW_ENABLE_LLVM_CPP_POC` default OFF. Saat ON, CMake menambahkan adapter internal dan executable `claw-llvm-poc` dengan LLVM major 18. Header adapter tidak mengekspos tipe LLVM; include/definitions LLVM privat pada implementasi. Driver memakai loader, sema, ownership, OIR, dan LIR Cat yang ada.

Adapter membangun modul langsung melalui `LLVMContext`, `Module`, dan `IRBuilder`, mengatur triple/DataLayout dari TargetMachine native, menjalankan `verifyModule`, lalu menghasilkan object PIC. Dump `.ll` opsional bukan input parser IR. Adapter tidak menjalankan Clang atau linker. Test menjalankan `cc program.o -o program` sebagai langkah terpisah.

Irisan yang diuji: `Int32`, `Bool`, return `Unit`, fungsi/panggilan langsung scalar, local storage/assignment, shadowing termasuk parameter, enam perbandingan, `not`, branch/goto, join kontrol hasil `if`, dan wrapper native `main`. Nama simbol internal qualified Cat dipertahankan agar tidak berbenturan dengan wrapper native. Signature, tipe operand, ABI, label, dan entry diperiksa sebelum konstruksi LLVM yang berisiko.

Input PoC dibatasi pada satu file/modul standalone. Aritmetika, pembagian, ref/borrow ABI, owned/drop, aggregate/static, runtime builtins, value phi, import/workspace, dan tipe lain ditolak eksplisit; tidak ada fallback tersembunyi. Template builtin `Result`/`Maybe` yang otomatis hadir pada LIR hanya dilewati bila struktur template cocok; pemakaiannya belum didukung.

## Bukti

Validasi dilakukan pada x86-64 GNU/Linux dengan LLVM/Clang 18.1.3 dan compiler host GCC 13.3.0:

- Build default berhasil dengan `CMAKE_DISABLE_FIND_PACKAGE_LLVM=TRUE`; binary `claw` sebelum/sesudah perubahan CMake identik SHA-256 dan tidak menautkan `libLLVM`/`libclang`.
- `CLAW_EXE=... bash claw/tests/run_all_tests.sh` lulus seluruh 16 suite produksi pada Linux. Ini bukan bukti eksekusi native Windows.
- CTest PoC lulus 2/2 gerbang: adapter dan diferensial end-to-end.
- Gerbang end-to-end: **41 kasus lulus** — 15 diferensial terhadap backend produksi, 16 penolakan, 9 I/O, dan 1 isolasi executable toolchain.
- Unit test langsung pada adapter menolak malformed LIR/ABI sebelum memublikasikan output, serta memeriksa emisi valid, reset pesan error, dan preservasi output lama saat penolakan.
- Emisi object berhasil dengan `PATH` kosong, tanpa executable Clang/compiler C/linker. Object baru kemudian ditautkan terpisah.
- Output salah/unsupported tidak diterbitkan. Kedua file output di-stage sebelum publikasi; tes kegagalan staging memastikan artefak lama tetap utuh dan temporary file dibersihkan.

Instruksi reproduksi dan batas cakupan: [README PoC](../claw/experimental/llvm_cpp/README.md). Artefak tes disimpan pada direktori build/evidence, bukan dicampurkan ke source atau mengganti intentional artifact behavior suite produksi.

## Batas keputusan

`cat-northstar/PRD.md` tetap otoritatif. Tidak ada fitur bahasa yang diratifikasi ulang atau dipromosikan melalui eksperimen ini. Belum ada bukti kesetaraan backend penuh, performa, overflow debug/release, runtime/ownership ABI, Windows UCRT64, atau kesiapan produksi. Dua file output tidak diklaim sebagai satu transaksi filesystem crash-atomic.

Jalur produksi tetap emitter LLVM IR tekstual → Clang codegen/link. Promosi backend membutuhkan bukti lanjutan dan keputusan tersendiri.
