# PoC backend LLVM C++ API Cat

Eksperimen ini menguji jalur **frontend Cat → ownership → OIR → LIR → LLVM C++ API → native object**, lalu linking sebagai langkah terpisah. Ini bukan penggantian backend produksi, bukan frontend C/C++, dan bukan perubahan semantik bahasa. `cat-northstar/PRD.md` tetap otoritatif.

Baseline: `74fab2e39e301a25b4a6ff97fa7d84f898e6fce1`.

## Isolasi

`CLAW_ENABLE_LLVM_CPP_POC` default **OFF**. Build biasa tetap menghasilkan `claw` dengan emitter LLVM IR tekstual dan Clang untuk codegen/link, tanpa memerlukan LLVM development SDK. Ketika diaktifkan, target tambahan `claw-llvm-poc` menggunakan adapter internal `adapter.h`; tipe dan header LLVM hanya ada pada implementasi adapter. Tidak ada fallback otomatis ke backend lama.

Adapter menerima `LirProgram`, membentuk modul melalui LLVM C++ API, memasang target triple dan DataLayout dari TargetMachine, memverifikasi modul, lalu menulis object native. File `.ll` opsional merupakan dump modul hasil API, **bukan input parser LLVM IR**. Adapter tidak meluncurkan Clang, compiler C, atau linker.

Bahasa Cat tetap memiliki parser, analisis tipe, ownership, dan lowering sendiri. Menggunakan LLVM sebagai mesin backend tidak menjadikan Cat turunan semantik C/C++. Eksperimen ini juga belum membuktikan compiler mandiri tanpa dependency LLVM, runtime, atau toolchain linker.

## Menjalankan

Prasyarat: host x86-64 GNU/Linux, LLVM development SDK **exact 23.1.3**, CMake, compiler C++20, Python 3, dan `cc` untuk langkah link. Clang diperlukan hanya untuk pembanding jalur produksi pada tes diferensial. LLVM18, versi23 lain, header/library campuran, dan SDK parsial bukan pengganti yang diterima.

[SDK resmi Linux X64 LLVM23.1.3](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.3) menyediakan component libraries statik. PoC tidak memerlukan target CMake monolithic `LLVM` atau seluruh metadata CMake SDK: `CLAW_LLVM_CONFIG` menunjuk executable SDK yang sebenarnya. CMake meminta komponen `core`, `codegen`, `mc`, `target`, `x86codegen`, `x86desc`, dan `x86info`, serta closure dependency dan system libraries dari SDK tersebut. Path setiap library wajib ada; tidak ada pencarian ulang ke LLVM18. Include/definitions/link dependency tetap privat di adapter.

Helper berikut mengunduh archive resmi dengan SHA256 yang dipin, hanya mengekstrak header, `llvm-config`, library LLVM statik, dan notice/lisensi. Tidak membangun LLVM dari source. Archive sekitar1,19GB; subset SDK sekitar380MB, di luar dependency sistem. Helper memerlukan Python package `zstandard`; pada Ubuntu juga siapkan `libxml2-dev`, `zlib1g-dev`, dan `libzstd-dev` untuk system libraries SDK.

```bash
python3 claw/experimental/llvm_cpp/fetch_sdk.py \
  --destination "$HOME/.cache/cat-llvm-23.1.3"
cmake -S claw -B claw/build/llvm-cpp-poc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCLAW_ENABLE_LLVM_CPP_POC=ON \
  -DCLAW_LLVM_CONFIG="$HOME/.cache/cat-llvm-23.1.3/bin/llvm-config"
cmake --build claw/build/llvm-cpp-poc -j4
ctest --test-dir claw/build/llvm-cpp-poc --output-on-failure
```

Pin diperiksa saat configure, pada header adapter saat compile, dan melalui probe versi dari library LLVM yang benar-benar ditautkan. SDK version mismatch harus gagal, bukan fallback. API23 memakai `llvm::Triple` bertipe dan `getTerminatorOrNull()` saat memeriksa blok yang belum memiliki terminator; `getTerminator()` pada LLVM23 mensyaratkan blok well-formed.

Contoh sumber `answer.cat`:

```cat
fn choose(x: Int32) -> Int32 {
    val result = if x < 4 { 17 } else { 29 }
    return result
}

fn main() -> Int32 {
    return choose(3)
}
```

```bash
claw/build/llvm-cpp-poc/experimental/llvm_cpp/claw-llvm-poc \
  answer.cat -o answer.o --emit-ir answer.ll
cc answer.o -o answer
./answer
echo $?  # 17
```

Langkah `cc` hanya menerima object dan melakukan linking. Tidak ada file source Cat, LLVM IR, atau runtime C yang dikirim ke langkah tersebut. Library startup/platform masih disediakan toolchain sistem.

Jalankan gerbang PoC beserta pembanding produksi:

```bash
python3 claw/experimental/llvm_cpp/test_poc.py \
  --poc claw/build/llvm-cpp-poc/experimental/llvm_cpp/claw-llvm-poc \
  --claw claw/build/llvm-cpp-poc/claw \
  --artifacts claw/build/llvm-cpp-poc/poc-artifacts
```

Artefak sumber, `.ll`, `.o`, executable, log, dan `results.json` disimpan agar bisa diperiksa. Skrip menguji exit status terhadap nilai yang diharapkan sekaligus terhadap jalur produksi; stdout/stderr juga dibandingkan dan wajib kosong untuk fixture scalar ini. Header object harus ELF64 little-endian relocatable x86_64, IR triple harus `x86_64-unknown-linux-gnu`, dan DataLayout harus sesuai x86_64 ELF. Triple, DataLayout, format, serta ukuran object dicatat di hasil. Tes negatif memastikan penolakan disertai diagnostik dan tidak menghasilkan object/IR baru.

CTest juga menjalankan `test_adapter.cpp` langsung pada API internal: signature/arity salah, entry invalid, operand bertipe salah, literal di luar rentang, label tak dikenal, value phi, ABI ref/indirect, dan instruksi unsupported harus ditolak sebelum output diterbitkan. Tes `PATH` kosong membuktikan emisi object tidak membutuhkan executable compiler/linker eksternal.

Object dan dump IR ditulis ke temporary file terlebih dahulu; kedua penulisan diperiksa sebelum publikasi. Penolakan LIR atau kegagalan staging tidak mengubah artefak lama, dan rename tiap file bersifat atomik. Dua nama file bukan satu transaksi filesystem yang crash-atomic: jangan mengasumsikan rollback pasangan object/IR bila proses mati atau filesystem berubah di antara dua rename.

## Batas tahap pertama

Input driver hanya file `.cat` standalone dengan satu modul. Entry tetap `fn main()` tanpa parameter dan berakhir `Unit` atau `Int32`. Driver memakai SemanticAnalyzer dan OwnershipChecker Cat yang ada, bukan pemeriksaan tiruan khusus contoh.

Subset yang dituju: nilai scalar `Int32`/`Bool`/`Unit`, fungsi dan pemanggilan langsung scalar, binding lokal, assignment, identitas binding saat shadowing, perbandingan, `not`, branch/goto, return, dan join kontrol hasil lowering `if`. Metadata phi `Control` pada LIR bukan phi nilai LLVM; nilai hasil `if` saat ini lewat storage lokal sebagaimana LIR Cat. Jangan menyimpulkan dukungan semua bentuk phi atau optimasi SSA dari contoh tersebut.

Aritmetika signed, pembagian/remainder, runtime builtins seperti `println`, owned values/drop glue, aggregate, static, float, ref/borrow ABI, FFI, import/workspace, dan cross-compilation bukan cakupan penerimaan tahap ini. Jalur yang belum didukung harus gagal eksplisit. Khusus aritmetika signed dan pembagian, lebih baik ditolak daripada menghasilkan LLVM operation yang melanggar aturan overflow atau defect pembagi nol dalam PRD.

Penyimpanan lokal/parameter `Unit`, tipe `UInt32`, value phi, dan pemanggilan rekursif langsung juga belum didukung. Panggilan ke entry `main` ditolak, termasuk dari helper. Diagnostik pembatasan PoC tidak menjanjikan format/stage yang identik dengan driver produksi.

Tidak ada profil debug/release bahasa yang baru. `CMAKE_BUILD_TYPE=Release` mengoptimalkan binary **compiler PoC**, bukan menyatakan seluruh kebijakan release Cat sudah diterapkan. Tidak ada klaim kecepatan, kesetaraan seluruh bahasa, dukungan Windows, atau kesiapan produksi.

Workflow opt-in PoC mengaktifkan flag hanya pada job khusus; workflow produksi lama tetap menguji jalur default. Suite produksi penuh dan sanitizer tidak boleh diskip atau diubah agar eksperimen hijau. Kegagalan diagnostik Windows baseline-vs-fixed yang menuntut kode stack-overflow pada baseline lama harus dilaporkan terpisah bila baseline menghasilkan access-violation; oracle tersebut tidak dilonggarkan.

## Syarat sebelum mempertimbangkan migrasi

1. Tambahkan dukungan semantik per irisan, bersama tes diferensial positif/negatif, bukan silent fallback.
2. Buktikan overflow debug/release, pembagi nol, short-circuit, bounds check, ownership/drop, layout, dan ABI sesuai PRD.
3. Uji runtime dan linking terpisah, termasuk target Windows UCRT64 yang belum divalidasi di PoC Linux ini.
4. Jalankan regresi penuh, sanitizer, benchmark, dan review independen pada snapshot yang akan dipromosikan.
5. Minta keputusan promosi tersendiri. PoC yang lulus tidak otomatis mengizinkan mengganti jalur produksi.
