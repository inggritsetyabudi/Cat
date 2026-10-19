# PoC backend LLVM C++ API Cat

Eksperimen ini menguji jalur **frontend Cat → ownership → OIR → LIR → LLVM C++ API → native object**, lalu linking sebagai langkah terpisah. Ini bukan penggantian backend produksi, bukan frontend C/C++, dan bukan perubahan semantik bahasa. `cat-northstar/PRD.md` tetap otoritatif.

Baseline: `74fab2e39e301a25b4a6ff97fa7d84f898e6fce1`.

## Isolasi

`CLAW_ENABLE_LLVM_CPP_POC` default **OFF**. Build biasa tetap menghasilkan `claw` dengan emitter LLVM IR tekstual dan Clang untuk codegen/link, tanpa memerlukan LLVM development SDK. Ketika diaktifkan, target tambahan `claw-llvm-poc` menggunakan adapter internal `adapter.h`; tipe dan header LLVM hanya ada pada implementasi adapter. Tidak ada fallback otomatis ke backend lama.

Adapter menerima `LirProgram`, membentuk modul melalui LLVM C++ API, memasang target triple dan DataLayout dari TargetMachine, memverifikasi modul, lalu menulis object native. File `.ll` opsional merupakan dump modul hasil API, **bukan input parser LLVM IR**. Adapter tidak meluncurkan Clang, compiler C, atau linker.

Bahasa Cat tetap memiliki parser, analisis tipe, ownership, dan lowering sendiri. Menggunakan LLVM sebagai mesin backend tidak menjadikan Cat turunan semantik C/C++. Eksperimen ini juga belum membuktikan compiler mandiri tanpa dependency LLVM, runtime, atau toolchain linker.

## Menjalankan

### Linux x86_64 ELF

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

### Windows native x64 UCRT / COFF

Konfigurasi Windows dibatasi **native x64 MinGW UCRT**. CMake memeriksa host/compiler x64, macro `_UCRT`, SDK Windows GNU exact23.1.3, dan component archives statik; adapter memeriksa triple/COFF/DataLayout. MSVC, MSVCRT, ARM64, cross-compilation, Wine, dan ABI FFI bukan cakupan. Hasil yang diterima harus berasal dari job **Windows native UCRT64 LLVM C++ PoC**, flag ON, pada exact SHA yang dinilai—CI produksi lama dengan PoC OFF bukan bukti ini.

[Paket SDK MSYS2 UCRT](https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-llvm) versi `23.1.3-1` menyediakan static component archives. Workflow memverifikasi SHA256 paket `d23bf19409cbafe837b9ef04b29943e81955eda2842e866ac0c77ef3126be569`, versi executable/header/library, dan menyimpan daftar paket/library aktual. Perubahan rolling package yang tidak cocok harus gagal, bukan turun versi diam-diam atau memakai SDK MSVC.

Pada shell MSYS2 **UCRT64**, setelah memasang SDK exact tersebut, Clang/CMake/Ninja, serta native UCRT Python:

```bash
cmake -S claw -B claw/build/llvm-cpp-poc-win -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/ucrt64/bin/clang++.exe \
  -DCLAW_ENABLE_LLVM_CPP_POC=ON \
  -DCLAW_LLVM_CONFIG=/ucrt64/bin/llvm-config.exe \
  -DPython3_EXECUTABLE=/ucrt64/bin/python.exe
cmake --build claw/build/llvm-cpp-poc-win -j2
ctest --test-dir claw/build/llvm-cpp-poc-win --output-on-failure
```

Harness yang sama tidak mengganti fixture/oracle Linux: object harus AMD64 COFF relocatable (`0x8664`, optional-header size0), triple `x86_64-w64-windows-gnu`, DataLayout little-endian64-bit dengan mangling Windows. Linker menerima **hanya object** pada langkah terpisah; output `.exe` harus PE32+ AMD64, dieksekusi native, dan exit/stdout/stderr dibandingkan terhadap nilai fixture serta jalur produksi. Command link/run dan DataLayout lengkap ada pada `results.json`; sample `.o`, `.ll`, `.exe` serta laporan `llvm-readobj` disimpan sebagai bukti CI.

Tes isolasi Windows menyalin emitter dan closure DLL non-sistem yang diaudit lewat COFF imports ke direktori tersendiri; child PATH hanya berisi direktori itu, bukan UCRT `bin` yang juga memuat Clang/linker. DLL sistem tetap dilayani loader Windows. `llvm-readobj` hanya alat audit setup tes, tidak dipanggil adapter untuk menghasilkan object. Gate Linux tetap menggunakan PATH kosong. Suite produksi/PTY lama tetap memakai Python MSYS; CTest PoC memakai Python Windows native.

Penambahan konfigurasi bukan bukti kelulusan: bila gate native ini gagal, jangan menyimpulkan dukungan Windows dari ketersediaan SDK atau dari hasil Linux. Scope yang boleh dilaporkan hanya target yang lulus pada exact SHA tersebut.

## Batas tahap pertama

Input driver hanya file `.cat` standalone dengan satu modul. Entry tetap `fn main()` tanpa parameter dan berakhir `Unit` atau `Int32`. Driver memakai SemanticAnalyzer dan OwnershipChecker Cat yang ada, bukan pemeriksaan tiruan khusus contoh.

Subset yang dituju: nilai scalar `Int32`/`Bool`/`Unit`, fungsi dan pemanggilan langsung scalar, binding lokal, assignment, identitas binding saat shadowing, perbandingan, `not`, branch/goto, return, dan join kontrol hasil lowering `if`. Metadata phi `Control` pada LIR bukan phi nilai LLVM; nilai hasil `if` saat ini lewat storage lokal sebagaimana LIR Cat. Jangan menyimpulkan dukungan semua bentuk phi atau optimasi SSA dari contoh tersebut.

Aritmetika signed, pembagian/remainder, runtime builtins seperti `println`, owned values/drop glue, aggregate, static, float, ref/borrow ABI, FFI, import/workspace, dan cross-compilation bukan cakupan **default scalar tanpa flag runtime/aritmetika**. Eksperimen runtime terpisah di bawah membuka `println(Int32)`; slice aritmetika berikut memakai flag tersendiri. Keduanya bukan seluruh builtin atau String/ownership ABI. Jalur yang belum didukung harus gagal eksplisit, bukan fallback.

Penyimpanan lokal/parameter `Unit`, tipe `UInt32`, value phi, dan pemanggilan rekursif langsung juga belum didukung. Panggilan ke entry `main` ditolak, termasuk dari helper. Diagnostik pembatasan PoC tidak menjanjikan format/stage yang identik dengan driver produksi.

Tidak ada profil debug/release bahasa yang baru. `CMAKE_BUILD_TYPE=Release` mengoptimalkan binary **compiler PoC**, bukan menyatakan seluruh kebijakan release Cat sudah diterapkan. Tidak ada klaim kecepatan, kesetaraan seluruh bahasa, dukungan Windows umum di luar kontrak/gate UCRT di atas, atau kesiapan produksi.

Workflow opt-in PoC mengaktifkan flag hanya pada job khusus; workflow produksi lama tetap menguji jalur default. Suite produksi penuh dan sanitizer tidak boleh diskip atau diubah agar eksperimen hijau. Kegagalan diagnostik Windows baseline-vs-fixed yang menuntut kode stack-overflow pada baseline lama harus dilaporkan terpisah bila baseline menghasilkan access-violation; oracle tersebut tidak dilonggarkan.

## Syarat sebelum mempertimbangkan migrasi

1. Tambahkan dukungan semantik per irisan, bersama tes diferensial positif/negatif, bukan silent fallback.
2. Buktikan overflow debug/release, pembagi nol, short-circuit, bounds check, ownership/drop, layout, dan ABI sesuai PRD.
3. Uji runtime dan linking terpisah di setiap target; bukti subset scalar UCRT tidak membuktikan ABI runtime, ownership, atau seluruh bahasa di Windows.
4. Jalankan regresi penuh, sanitizer, benchmark, dan review independen pada snapshot yang akan dipromosikan.
5. Minta keputusan promosi tersendiri. PoC yang lulus tidak otomatis mengizinkan mengganti jalur produksi.

## Link object tanpa compiler driver: LLD PoC terpisah

[PoC LLD exact23.1.3](lld/README.md) menambahkan opsi `CLAW_ENABLE_LLD_POC=OFF` default untuk membandingkan executable `ld.lld` dan worker API `lld::lldMain`, dengan CRT/platform inputs eksplisit. Contoh `cc` di atas tetap pembanding asli, bukan fallback. Linker baru hanya diuji pada subset object yang sama dan tidak mengubah driver produksi atau menghilangkan kebutuhan runtime/SDK sistem. Lihat kontrak platform, argv, isolasi tanpa Clang, ukuran dependency dan gate native dalam README linker.

## Runtime output melalui paket prebuilt terpisah

[Eksperimen runtime Int32](runtime/README.md) menambahkan `CLAW_ENABLE_RUNTIME_POC=OFF` default untuk menyiapkan archive target-native dari runtime C produksi yang tidak dimodifikasi. Persiapan boleh memakai compiler C; **emit/link per source tidak boleh**. Emitter meminta `--runtime-i32`, lalu external LLD menghubungkan object dan archive runtime yang sama, dengan CRT/SDK inputs eksplisit. Default 41 kasus scalar/rejection lama tidak diubah. Flag ini sendiri tetap tidak membuka String/Str atau alokasi/drop owned; irisan String di bawah memerlukan flag terpisah. Tidak ada klaim compiler sepenuhnya hermetik atau CRT statik.

## Slice aritmetika Int32 native, opt-in

`--arithmetic-i32` (API `ObjectOptions.enableArithmeticI32`, default `false`) membuka satu irisan signed **Int32**: `+`, `-`, `*`, unary `-`, `/`, dan `%`. `--runtime-i32` tetap diminta terpisah bila program memanggil `println(Int32)`. Tidak ada perubahan emitter, runtime, ABI, atau driver produksi.

**Batas semantik penting:** [PRD §6/§19](../../../cat-northstar/PRD.md) sudah meratifikasi Debug overflow = defect dan Release = wrapping. Namun [profil produksi saat ini](../../../docs/CLI.md#build-output-and-runtime) hanya membedakan optimisasi O0/O2; lowering native secara eksplisit masih release-only dan Debug safety [staged di v0.8](../../../docs/v0.1-v0.3-acceptance-matrix.md). Slice ini mengikuti **perilaku native produksi tersebut**, bukan membuat profil bahasa baru atau mengklaim Debug overflow telah tersedia. Kesamaan hasil produksi O0/O2 bukan penerimaan dua profil semantik bahasa.

| Operasi | Kontrak slice |
| --- | --- |
| add/sub/mul/neg | Wrapping 32-bit, ditafsirkan signed Int32; LLVM tanpa `nsw`/`nuw`. |
| div/rem nonzero | LLVM `sdiv`/`srem`, pembagian menuju nol dan tanda remainder mengikuti dividend. Tidak ada `udiv`/`urem`. |
| divisor nol | Guard sebelum operasi invalid, memanggil hook C `claw.runtime.defect.division_by_zero()` yang `noreturn`, lalu `unreachable`; bukan pembagian hardware atau hasil fiktif. |
| MIN / -1 dan MIN % -1 | Hasil release yang diratifikasi: MIN dan 0. Divisor aman dipilih sebelum pembagian sehingga edge LLVM invalid tidak terbentuk, termasuk literal dan blok mati. |

Contoh sumber yang benar-benar melewati frontend Cat:

```cat
fn calculate(a: Int32, b: Int32) -> Int32 { return (a + b) * 2 }
fn main() { println(calculate(20, 1)) }
```

```bash
claw-llvm-poc arithmetic.cat -o arithmetic.o --emit-ir arithmetic.ll \
  --runtime-i32 --arithmetic-i32
# Link object + archive runtime target-native dengan LLD dan CRT eksplisit;
# argv teresolusi disimpan oleh harness, bukan diganti dengan cc/Clang.
ctest --test-dir <build> -R llvm-cpp-poc-arithmetic --output-on-failure
```

Gate `runtime/test_arithmetic.py` menyiapkan referensi produksi O0 dan `--release` sebelum isolasi, lalu membandingkan stdout/stderr/exit native, IR signed/guard/flag, dan ELF/PE. Emit/link/run memakai PATH tanpa compiler, hanya object + runtime archive immutable + CRT/SDK pada final link. Manifest, hash dan mtime runtime harus tetap identik. Sumber Cat, IR, command/output, audit dependency, ukuran dan timing disimpan di `runtime/artifacts/arithmetic/testdata.json` beserta artefaknya. Gate berjalan pada Linux Release/sanitizer-host dan Windows UCRT native; menambahkan gate bukan klaim lulus sebelum hasil exact-head tersedia.

Lebar integer lain, unsigned, float, bitwise, owned/String, FFI, arbitrary value phi dan canonical Debug checked overflow tetap di luar cakupan. Default 41 kasus dan gate runtime sebelumnya dipertahankan; malformed LIR tetap harus ditolak sebelum publikasi object/IR. Paket runtime C tetap dikompilasi hanya pada persiapan, bukan per source. Sanitizer host menginstrumentasi compiler C++; ini bukan klaim bahwa archive runtime C atau semua executable target ikut terinstrumentasi.

## String/Str eksperimental bounded, opt-in

`--strings` (`ObjectOptions.enableStrings=false` secara default) membuka irisan
tambahan, bukan dukungan seluruh String atau kesiapan produksi. Ketiga opsi build
PoC tetap OFF secara default; emitter/runtime/ABI produksi tidak berubah.

Yang diterima: literal `Str` statis (empty, UTF-8 multibyte, embedded NUL),
`println(Str)`, `String.from(Str)`, `.as_str()`, `.clone()` eksplisit, move/return
owned melalui helper, argumen scalar, serta cleanup yang sudah diterbitkan
ownership→OIR→LIR Cat. Tidak ada ownership analyzer pengganti, implicit clone,
realloc, atau copy berbasis terminator. Constructor memakai `max(len,1)` dan
menyalin tepat `len` byte; clone menyalin capacity/length produksi dan membuat
alokasi baru. Borrow view tidak mengalokasikan maupun melakukan drop.

ABI mengikuti paired oracle `116299f0b09ab54b94545c3977396a705605315a`, bukan
ABI C aggregate yang ditebak: `%claw.slice = {ptr,i64}`, `%claw.buffer =
{ptr,i64,i64}`, ukuran/alignment 16/8 dan 24/8 diperiksa dengan DataLayout.
Helper `Str -> Str` memakai parameter aggregate langsung pada Linux, pointer
record pada Windows GNU/UCRT, dan return aggregate pada keduanya. Helper String
memakai argumen pointer record serta hidden result-slot pointer pertama dengan
return `void`; tidak ditambahkan `sret`, `byval`, `nonnull`, atau atribut ABI lain.
Runtime tetap `alloc(i64)->ptr`, `copy(ptr,ptr,i64)`, `free(ptr)->void`, serta
`println.slice(ptr-record)->void`, dengan signed length/capacity i64.

Storage dan spill dihoist ke entry, tetapi store/initialization tetap di titik
LIR asli. Ini mencegah penambahan stack per iterasi. Drop memakai pointer owner
yang dirujuk binding ID; tidak disintesis ulang dari nama atau urutan source.
Normal/early return, temporary full-expression, nested scope, branch tak ambigu,
loop kondisi Bool, `skip`/`stop`, clone setelah owner asal mati, dan tracked local/
parameter shadowing diuji. Metadata phi `Control` bukan arbitrary aggregate phi.

**Batas fail-closed penting:** LIR saat ini dapat tidak memberi ID unik untuk
dua owner bernama sama pada sibling branch. Bentuk itu ditolak sebagai duplicate
binding; adapter tidak menebak provenance atau melemahkan guard. `Result`, choice,
pick/try, scan/range iterator, generic collections, arbitrary value phi, comparison
String, slice/index/range view, borrowed shape fields, general ref/FFI/import,
Arena, f-string/Show tetap tidak didukung. Tidak ada pengecualian borrowed field,
lifetime syntax baru, atau perubahan Arena.

```cat
fn relay(value: String) -> String { return value }
fn main() {
    val owner = relay(String.from("Aé\0Z"))
    val copied = owner.clone()
    println(copied.as_str())
}
```

```bash
claw-llvm-poc example.cat -o example.o --emit-ir example.ll --strings
# Link example.o + archive runtime target-native + CRT/SDK memakai LLD.
ctest --test-dir <build> -R llvm-cpp-poc-strings --output-on-failure
```

Gate terpisah menyimpan 19 program positif/38 alokasi per run observer, referensi
produksi O0/O2 byte-exact (termasuk CRLF target), OIR/LIR/IR, object, link argv,
dependency audit, timing/ukuran, OOM forced-allocation-failure, missing archive/
runtime-symbol, conflicting ownership, dan unsupported-feature rejections.
`string-legacy-contracts.json` mengunci hash 41 kasus lama, source 29 program
aritmetika, serta 14-case/63-allocation oracle yang tidak dimodifikasi.

Observer test-only menggunakan malloc/free nyata; kontrol sengaja leak dan
untracked free harus ditolak validator. Linux juga menautkan archive runtime C
terinstrumentasi ASan/UBSan/LSan dengan LLD. Archive sanitizer **tanpa registry
observer** agar pointer registry tidak menyamarkan leak; kontrol leak nyata wajib
ditemukan LSan. Toolchain C instrumentation dicatat terpisah dari LLVM/LLD23.1.3
backend. Ini bukan klaim semua instruksi LLVM hasil adapter terinstrumentasi.
Windows melakukan probe compile/link/run runtime lengkap untuk tiap sanitizer;
ketidaktersediaan dilaporkan SKIP dengan diagnostic, bukan PASS. Native byte,
ABI, cleanup dan observer Windows tetap wajib. Hasil yang boleh diklaim harus
berasal dari exact SHA yang lulus CI native, bukan hasil Linux saja.

## Oracle ABI/cleanup String produksi — tanpa dukungan adapter baru

[Paired native String/Str oracle](abi_oracle/README.md) menambahkan gate evidence-only untuk raw Cat ABI, compiled C layouts, native allocation/free cleanup dan sanitizer availability Linux/Windows UCRT pada exact SHA yang sama. Gate juga menuntut penolakan String/Str adapter pada default dan seluruh flag I32 tanpa publikasi object/IR; kontrol scalar tetap harus berhasil. Ini tidak mengaktifkan String, aggregate ABI, borrowed values atau owned-drop pada adapter. 11 gate lama tetap utuh; hasil oracle hanya dapat dipakai untuk menentukan scope implementasi PoC berikutnya, bukan klaim production-ready.
