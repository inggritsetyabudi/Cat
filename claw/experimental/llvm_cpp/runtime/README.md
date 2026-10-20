# Runtime Cat prebuilt untuk PoC LLVM/LLD

Eksperimen ini hanya menambahkan output **`println(Int32)`** pada adapter opsional. Backend, runtime C, ABI dan semantik produksi tidak diubah. `CLAW_ENABLE_RUNTIME_POC` default **OFF**, memerlukan kedua opsi LLVM C++ dan LLD PoC. `--runtime-i32` pada emitter juga harus diminta secara eksplisit; tanpa flag tersebut, kontrak 41 kasus PoC lama tetap sama, termasuk penolakan pemanggilan runtime.

## Batas semantik yang diaudit

`runtime/native_runtime.c` mendeklarasikan simbol asm `claw.runtime.println.i32` sebagai `void(int32_t)`; implementasinya memakai `fprintf(stdout, "%d\n", value)` lalu `fflush(stdout)`. Emitter produksi mendeklarasikan `void(i32)` tanpa aggregate/pointer ABI atau atribut extension. Adapter baru harus menerima hanya metadata LIR `call.runtime println`, hook `runtime.println`, satu argumen `Int32`, hasil `Unit`, tanpa result binding maupun metadata external/builtin. Metadata yang tidak cocok ditolak, bukan ditebak.

`Int32` adalah scalar plain: tidak ada transfer owner, heap atau drop glue Cat. Tes memastikan tidak ada call/alur owned Cat yang diam-diam disisipkan; alokasi internal stdio/startup CRT tetap boleh ada. Berakhir normal dan flushing output bukan bukti dukungan ownership bahasa secara umum.

**Berhenti di Int32.** PRD §12 membedakan literal `Str` borrowed `{ptr,len}` dari `String` owned `{ptr,len,cap}`, dengan copy/move/borrow/drop yang wajib benar. Tidak ada perubahan adapter untuk layout atau cleanup tersebut. Literal teks, `String.from`, buffer, anchor, arena, refs, `print`, Bool dan overload lain tetap di luar irisan ini. Tidak ada klaim leak/double-free/drop parity untuk nilai owned yang belum didukung.

## Dua fase yang tidak boleh dicampur

1. **Preparation/package, toolchain normal:** CMake mengompilasi source runtime produksi **utuh dan tidak dimodifikasi**, sekali per target/configuration ketika dibutuhkan build system, lalu membuat archive native target-specific. Tidak ada salinan fungsi printf buatan PoC. Function/data sections memungkinkan section GC saat link. Archive utuh masih berisi fungsi runtime lain; keberadaannya bukan acceptance semantik adapter. Hash source/object/archive, compiler/target, simbol dan command preparation disimpan sebagai provenance.
2. **Per source, compiler tidak tersedia:** frontend/ownership/OIR/LIR Cat → LLVM C++ API → native object, lalu **external LLD23.1.3** menerima object, archive runtime prebuilt dan CRT/platform inputs yang eksplisit. Tidak ada `.cat`, `.ll`, atau runtime `.c` yang menjadi input compiler driver pada fase ini. Dump `.ll` hanya artefak audit. Hash/mtime package tidak boleh berubah setelah emit/link/run beberapa source.

Linux memakai PATH kosong. Windows menyalin emitter/linker beserta closure DLL non-sistem yang diaudit ke direktori tersendiri; PATH tidak memuat direktori toolchain. Audit/setup dan pembanding produksi dilakukan sebelum/di luar fase terisolasi. Itu tidak sama dengan klaim filesystem sandbox yang melarang semua executable melalui absolute path, atau paket OS yang sepenuhnya hermetik.

## Membangun dan menjalankan gerbang

Pada Linux **native Ubuntu24 x86_64, GCC13/glibc**, dengan SDK LLVM+LLD resmi exact23.1.3 yang sudah disiapkan melalui helper `fetch_sdk.py --with-lld`:

```bash
cmake -S claw -B claw/build/runtime-poc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-13 \
  -DCLAW_ENABLE_LLVM_CPP_POC=ON \
  -DCLAW_ENABLE_LLD_POC=ON \
  -DCLAW_ENABLE_RUNTIME_POC=ON \
  -DCLAW_LLVM_CONFIG="$SDK/bin/llvm-config" \
  -DCLAW_LLD_EXECUTABLE="$SDK/bin/ld.lld"
cmake --build claw/build/runtime-poc --parallel 2 --verbose
ctest --test-dir claw/build/runtime-poc --output-on-failure
CLAW_EXE="$PWD/claw/build/runtime-poc/claw" bash claw/tests/run_all_tests.sh
```

Pada **Windows native MinGW UCRT x64**, gunakan SDK/package exact yang dipin workflow, compiler C `/ucrt64/bin/clang.exe`, compiler C++ `/ucrt64/bin/clang++.exe`, `llvm-config.exe`, `ld.lld.exe`, dan Python Windows native. Hanya object AMD64 COFF dan PE32+ console yang diterima. Tidak ada dukungan MSVC, MSVCRT, Apple, ARM64, Wine atau cross-compilation yang dapat disimpulkan dari eksperimen ini.

Contoh source nyata yang tidak memerlukan operasi arithmetic baru:

```cat
fn show(value: Int32) {
    println(value)
}

fn main() -> Int32 {
    show(42)
    println(2147483647)
    return 0
}
```

Emisi opt-in:

```bash
env PATH="" "$EMITTER" output.cat --runtime-i32 -o output.o --emit-ir output.ll
```

Gerbang runtime merekam **argv link lengkap dan resolved CRT/library paths**, format object/executable, symbol definitions/undefined/imports, dependencies, stdout/stderr/exit, dan waktu emit/link/run pada artefak JSON. Jangan mengganti argv LLD dengan `cc`, atau menyalin contoh profil Linux ke Windows. Gerbang positif/negatif dan production comparison harus benar-benar lulus pada exact SHA sebelum platform dilaporkan terbukti.

## Apa yang tetap diperlukan

Archive runtime Cat statik **tidak berarti CRT statik**. Profil Linux tetap membutuhkan startup objects/GCC support serta glibc dan dynamic loader. Profil Windows tetap membutuhkan startup/support MinGW, UCRT import libraries dan Windows system DLLs. Inspeksi dependency final menentukan library dynamic yang sesungguhnya dipakai. Archive/runtime object kecil juga tidak menghilangkan footprint SDK, LLD dan closure DLL yang sudah dicatat milestone linker.

[Desain archive LLD](https://lld.llvm.org/NewLLD.html) menjelaskan lazy symbol extraction. [Driver MinGW exact23.1.3](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/lld/MinGW/Driver.cpp) menerjemahkan GNU-flavor ke COFF, termasuk opsi GC. Eksperimen memakai section GC agar runtime owned yang tidak dirujuk tidak menjadi bagian reachable output, bukan sebagai pengganti implementasi drop.

Scope sanitizer harus dibaca dari konfigurasi: flag C++/linker host menginstrumentasi compiler/worker yang dibangun, bukan otomatis seluruh runtime C/CRT/SDK prebuilt atau generated executable. Paket ini bukti debug/reproduksi, bukan release SDK universal. PR tetap draft dan promosi production memerlukan keputusan terpisah.
