# PoC linker LLD23.1.3, bukan penggantian jalur produksi

Object Cat dapat diberikan langsung ke **LLD**, tanpa Clang/`cc` driver pada langkah link. Itu **tidak menghilangkan linker, CRT, runtime sistem, SDK, atau kebutuhan memilih ABI**. Target dan tes di direktori ini terisolasi, default **OFF**, dan tidak dipanggil driver produksi `claw`.

Ada dua mode pembuktian: executable resmi `ld.lld`, dan worker `claw-lld-link-poc` yang menautkan library LLD statik lalu memanggil `lld::lldMain`. Worker ini proses terpisah; bukan embedding ke dalam proses compiler produksi. Tidak ada fallback ke Clang, `cc`, GNU ld, atau linker lain bila konfigurasi/LLD gagal.

## Kontrak yang diuji

Linux memakai ELF64 x86_64 native, profil **Ubuntu24/GCC13/glibc**, output non-PIE `ET_EXEC`. Windows memakai **native MinGW UCRT x64**, COFF relocatable menjadi PE32+ subsystem console. Keduanya GNU flavor: `ld.lld -flavor gnu -m elf_x86_64` atau `-m i386pep`. Profil adalah kontrak PoC yang eksplisit, bukan discovery toolchain universal atau sysroot hermetik.

`lld-link` adalah MSVC-flavor parser, bukan sinonim argumen GNU/MinGW. Ketersediaan dan versinya dicatat, tetapi **link MSVC, MSVCRT, Apple, ARM64, Wine, atau cross-compilation tidak diklaim**. Build object emitter Windows sebelumnya dan CI produksi PoC OFF bukan bukti linker ini. Hasil native harus berasal dari gate baru pada exact SHA yang dinilai.

## API dan library SDK resmi

Sumber [Driver.h exact23.1.3](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/include/lld/Common/Driver.h) mendefinisikan `DriverDef`, `Result{retCode,canRunAgain}`, dan `lldMain`. Worker mendaftarkan hanya ELF, COFF dan MinGW. GNU flavor mendispatch MinGW berdasarkan `-m i386pep`; lihat [dispatcher resmi](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/Common/DriverDispatcher.cpp). Mach-O/Wasm tidak diregistrasikan.

Library dipilih dari prefix LLVM23.1.3 yang sama: `lldELF`, `lldCOFF`, `lldMinGW`, `lldCommon` dan closure komponen statik yang diminta lewat `llvm-config --link-static --libfiles`. Daftar required components mengikuti union CMake upstream [Common](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/Common/CMakeLists.txt), [ELF](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/ELF/CMakeLists.txt), [COFF](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/COFF/CMakeLists.txt) dan [MinGW](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/MinGW/CMakeLists.txt). LLD/LTO menginisialisasi seluruh backend SDK, sehingga closure jauh lebih besar daripada emitter X86 saja; itu bukan klaim semua target sudah diuji.

Official Linux SDK membutuhkan **Polly dan PollyISL** dalam closure LTO/Passes. Helper SDK memilih arsip resmi itu juga; tidak boleh memakai stub `getPollyPluginInfo`, menghapus dependency, atau mencampur library18. Versi LLVM library dan LLD library dibuktikan lewat kode tertaut pada `--sdk-version`, bukan hanya konstanta header. Beberapa utility header SDK Linux merujuk generated `Version.inc` yang tidak terpasang; worker memakai entrypoint `Driver.h` dan meminta banner versi dari API asli, tanpa membuat generated header palsu.

Menurut [kontrak embedding LLD](https://lld.llvm.org/NewLLD.html), crash recovery/re-entry tidak berarti thread-safe. Worker normal membuat satu pemanggilan serial; bila `canRunAgain=false`, proses dihentikan lewat `exitLld`. Tes re-entry hanya success / option error / success, bukan pembuktian aman terhadap object berbahaya, concurrency, atau crash arbitrer. Integrasi dalam proses compiler, locking global dan containment object tidak tepercaya adalah keputusan lanjutan tersendiri.

## Dependency yang tidak dihapus

- **Linux:** `crt1.o`, `crti.o`, GCC13 `crtbegin.o`, object Cat, libc linker script, `libgcc.a`, `libgcc_s.so`, `crtend.o`, `crtn.o`, search paths dan dynamic interpreter `/lib64/ld-linux-x86-64.so.2`. Linker script libc sendiri menunjuk library sistem lain. Trace/map serta `readelf` merekam resolution, simbol dan `DT_NEEDED`; executable tetap memakai glibc/loader sistem.
- **Windows UCRT:** MinGW `crt2.o`, `crtbegin.o`, `crtend.o`, GCC support/unwind, `mingw32`, `moldname`, `mingwex`, UCRT dan import libraries Windows (`kernel32`, `advapi32`, `shell32`, `user32`). PE imports yang benar-benar terpakai diaudit, bukan disimpulkan hanya dari daftar library. DLL sistem Windows tetap diperlukan.
- **Cat:** subset scalar yang diuji tidak memanggil runtime Cat seperti `println`/allocator/drop. Keberhasilan subset ini tidak menghapus kebutuhan runtime tersebut untuk fitur lain atau membuktikan ABI FFI/ownership seluruh bahasa.
- **Alat platform:** C++ compiler/CMake untuk membangun compiler dan worker; Python untuk tes; `readelf`/`llvm-readobj` untuk audit; paket CRT/SDK/OS loader untuk link/run. Tool audit bukan bagian adapter dan tidak dipanggil untuk menghasilkan object.

## Linux: reproduksi

Dari root repo, gunakan SDK resmi yang sama untuk LLVM dan LLD:

```bash
python3 claw/experimental/llvm_cpp/fetch_sdk.py \
  --destination /tmp/cat-llvm23-lld-sdk --with-lld
cmake -S claw -B /tmp/cat-lld-build -DCMAKE_BUILD_TYPE=Release \
  -DCLAW_ENABLE_LLVM_CPP_POC=ON -DCLAW_ENABLE_LLD_POC=ON \
  -DCLAW_LLVM_CONFIG=/tmp/cat-llvm23-lld-sdk/bin/llvm-config \
  -DCLAW_LLD_EXECUTABLE=/tmp/cat-llvm23-lld-sdk/bin/ld.lld
cmake --build /tmp/cat-lld-build -j2
ctest --test-dir /tmp/cat-lld-build --output-on-failure
CLAW_EXE=/tmp/cat-lld-build/claw bash claw/tests/run_all_tests.sh
```

Helper memverifikasi archive resmi23.1.3 `LLVM-23.1.3-Linux-X64.tar.zst`, SHA256 `14d2f701eb68fb799001bdea6231048f3990690fa2f406d563555ff8f744daba` dan ukuran1,188,398,549 byte, lalu hanya mengekstrak anggota terpilih. Memerlukan decoder zstandard dengan window1GiB; tidak membangun LLVM dari source. Destination nonempty yang tidak punya marker/hash sesuai ditolak, bukan dihapus/ditimpa. Cache LLVM-only tidak diperlakukan sebagai cache LLVM+LLD.

## Windows: reproduksi

Di shell **MSYS2 UCRT64**, pasang SDK LLVM `23.1.3-1` sebagaimana README parent dan paket [LLD UCRT resmi](https://packages.msys2.org/package/mingw-w64-ucrt-x86_64-lld) `23.1.3-1`, SHA256 `88dd5b3cd86dbda5ea956a51f49066447123ac3b07526bc6becfe91b79189b8c` (workflow merekam hash dan paket aktual):

```bash
cmake -S claw -B build-lld-win -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/ucrt64/bin/clang++.exe \
  -DCLAW_ENABLE_LLVM_CPP_POC=ON -DCLAW_ENABLE_LLD_POC=ON \
  -DCLAW_LLVM_CONFIG=/ucrt64/bin/llvm-config.exe \
  -DCLAW_LLD_EXECUTABLE=/ucrt64/bin/ld.lld.exe \
  -DPython3_EXECUTABLE=/ucrt64/bin/python.exe
cmake --build build-lld-win -j2
ctest --test-dir build-lld-win --output-on-failure
```

Build memakai host C++ compiler secara normal; klaim tanpa compiler driver berlaku pada **link object Cat**, bukan bootstrap/build C++ keseluruhan.

## Apa yang dicatat tes

Empat gerbang parent tetap: versi SDK, configure contract, valid/malformed adapter, dan **41 kasus asli** (15 diferensial,16 rejection,9 I/O,1 object-emission isolation). Tidak ada fixture atau oracle lama yang dilonggarkan.

Empat gerbang tambahan: versi library LLD tertaut, serial re-entry, lima configure contract, dan native linker proof. Link proof memakai **15 object yang sama** per mode; membandingkan exit/stdout/stderr terhadap oracle dan executable pembanding lama; memastikan hash object tidak berubah; memeriksa triple/DataLayout/object dan output ELF/PE. Ditambah path literal, input malformed/library/startup hilang, serta sepuluh warm-link tiny fixture yang outputnya diverifikasi (bukan benchmark seluruh program).

Emisi object dengan PATH kosong tetap tes tersendiri. Link Linux baru juga memakai PATH kosong. Windows menyalin linker dan closure DLL non-sistem ke application directory terisolasi—bukan sekadar mengosongkan PATH sambil menyisakan Clang dalam direktori executable asli. Tidak ada Clang/`cc` yang dapat ditemukan melalui link PATH; DLL OS tetap ditangani loader. Audit DLL/ELF memakai tool absolut dalam fase terpisah.

Artefak `lld/artifacts/testdata.json`, trace/map, header/import/symbol reports memuat argv aktual, resolved input paths/hash/size, versi linker, ABI, exit/stdout/stderr serta sepuluh timing. `lld-sdk-provenance.txt` menyimpan closure library dan system libs aktual. Footprint harus menghitung worker/executable **beserta** dependency DLL/archive/SDK; official SDK penuh tidak boleh dibandingkan dengan binary compiler saja. LLVM/LLD berlisensi Apache-2.0 WITH LLVM-exception; notices dependency pihak ketiga/CRT dan syarat redistribusi masing-masing tetap harus ditinjau.

Rekomendasi awal adalah adapter object-only tetap terpisah dan **external LLD dengan profil CRT eksplisit** sebagai langkah integrasi berikutnya. Embedding API worker berguna sebagai bukti feasibility, tetapi membesarkan static closure dan menambah kontrak lifetime/crash/concurrency; bukan alasan untuk mengubah jalur produksi. Hasil CI exact-head dan review independen menentukan target mana yang boleh dilaporkan lulus. Kegagalan diagnostik baseline Windows lama `0xC0000005` terhadap oracle stack-overflow tetap dilaporkan terpisah, tidak diubah untuk menghijaukan PoC.

Rincian audit per flavor: ELF menerima `--trace`; MinGW menggunakan `--verbose`, yang [driver resmi23.1.3](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/MinGW/Driver.cpp) teruskan sebagai COFF `-verbose`. Kedua stream link dicatat bersama trace/map. Menyesuaikan opsi diagnostik ini tidak mengganti linker, runtime, fixture atau oracle, dan pengujian parser di Linux tidak menggantikan bukti link/run Windows native.
