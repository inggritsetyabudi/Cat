# Oracle native ABI dan cleanup String/Str — evidence only

Gate ini menjalankan **backend produksi** Cat pada Linux x86_64 ELF dan Windows native MinGW UCRT x64. Ini bukan implementasi String/Str pada adapter LLVM C++ API. Adapter tetap harus menolak seluruh input string, baik default maupun dengan semua flag I32, tanpa menerbitkan requested object atau IR.

## Kontrak yang dibandingkan

`cat-northstar/PRD.md` §12 menetapkan Str borrowed UTF-8 `{ptr,len}`, String owned affine immutable `{ptr,len,cap}`, explicit clone, tanpa sintaks lifetime source atau borrowed field pada shape normal. Oracle tidak mengubah kontrak, runtime ABI, Arena, f-string/Show, atau acceptance markers.

Layout C dibuktikan dengan meng-include runtime produksi yang tidak diubah dan mengompilasi static assertions pada **kedua host native**: Str size16/align8/offset0,8; String size24/align8/offset0,8,16. `len`/`cap` C adalah `int64_t`; tidak ada klaim universal bahwa semua bit pattern merupakan nilai valid.

Layout saja bukan calling convention. Dedicated fixture menguji caller/callee untuk Str identity, borrowed helper, String owned identity/transfer, constructor return, clone dan view. Expected marker harus membedakan Str aggregate parameter SysV dari pointer parameter Windows. String parameter/return memakai klasifikasi Cat indirect: pointer argument slot serta leading result pointer dengan LLVM return void. Ini Cat-internal sret-like convention, bukan atribut C `sret`/`byval` yang boleh ditambahkan secara spekulatif.

Runner menyimpan AIR/OIR/LIR dan LLVM IR produksi, triple/DataLayout, seluruh declaration/definition/call operands/attributes, linkage/runtime symbols, source bytes, output bytes dan exit. Ia membandingkan hasil dengan expected values, bukan sekadar menerima kesamaan dua compiler profiles. O0/O2 adalah pilihan optimisasi produksi, bukan profil safety bahasa baru.

## Cleanup dan instrumentasi

`observer_runtime.c` menginterpose malloc/free hanya dalam translation unit runtime produksi, memakai registry statis tanpa alokasi tambahan, dan tetap memanggil allocator/free sebenarnya. Event mencatat ukuran byte dan summary allocations/frees/outstanding/untracked. Constructor mendaftarkan callback atexit; startup/atexit native Windows harus benar-benar dijalankan oleh gate, bukan diasumsikan dari Linux.

`observer_selftest.c` mempunyai kontrol normal dan dua kontrol negatif: allocation yang sengaja tidak dibebaskan serta free atas allocation dari TU lain. Process exit saja tidak cukup: runner wajib menolak ledger outstanding/untracked walaupun executable mengembalikan 0. Kontrol negatif bukan memory leak pada program Cat yang dinyatakan PASS dan tidak diberi klaim clean-exit LSan.

Corpus memakai fixture produksi existing untuk temporary cleanup, branch/pick/scan/loop, early return, try failure, clone, empty/multibyte UTF-8/embedded NUL. Exact counts dan raw stdout wajib cocok pada O0/O2. Observer membuktikan balance untuk allocation runtime yang diamati; ia **bukan** pengganti ASan untuk seluruh use-after-free/out-of-bounds dan bukan global libc heap profiler.

Sanitizer options diprobe melalui compile **dan native run**. Linux wajib mempunyai target ASan/UBSan/LSan yang dibutuhkan. Windows mencatat dukungan aktual; unavailable menjadi SKIP eksplisit dengan diagnostic, tidak menjadi sanitizer PASS. Native UCRT observer merupakan bukti objektif allocation/free terpisah, bukan sanitizer.

## Menjalankan

Dengan konfigurasi PoC+LLD+runtime opt-in existing:

```bash
ctest --test-dir <build> -R '^llvm-cpp-production-string-abi-oracle$' --output-on-failure
```

Gate ke-12 ditambahkan; 11 gate lama, 41 kasus legacy, runtime dan arithmetic cases tidak diubah. Workflow menjalankan focused oracle dahulu, seluruh CTest, lalu full production suite unchanged dalam checkout disposable terpisah agar artifact tracked yang diregenerasi tidak mengotori checkout evidence. Hasil native Linux dan Windows harus berasal dari SHA PR head yang sama. SDK/LLD PoC tetap exact 23.1.3; compiler produksi/sanitizer dicatat terpisah.

Artefak berada di `<build>/experimental/llvm_cpp/lld/runtime/artifacts/abi-oracle` (ikuti path CMake aktual). Workflow mengunggah source, IR dan laporan JSON/txt di subtree runtime; executable tidak diperlukan untuk menafsirkan raw byte traces. Tidak ada String object→LLD→run dari adapter yang diklaim: output string berasal dari driver produksi, sedangkan object scalar merupakan kontrol positif adapter.

Kelulusan oracle hanya dapat menjadi bukti untuk memilih implementasi PoC bounded berikutnya. Itu bukan dukungan String adapter, general CFG/liveness proof, C FFI certification, atau izin migrasi produksi.

### DataLayout asli versus efektif

Emitter produksi saat milestone ini menulis target triple tetapi **tidak menulis `target datalayout`**. Oracle menyimpan fakta absensi itu sebagai `raw_ir.data_layout=null` dan tidak memodifikasi IR asli. Untuk melihat pilihan toolchain yang benar-benar memproses modul, ia menjalankan Clang native dengan `-S -emit-llvm -x ir -O0` terhadap IR tersebut dan menyimpan `clang-effective.ll` beserta command dan `effective_clang_ir`. Target triple vendor dapat dinormalisasi Clang; itu direkam terpisah. Ini bukan DataLayout yang diterbitkan Cat dan tidak boleh dilabeli demikian. ABI signature oracle menggunakan **IR Cat asli**, bukan hasil normalisasi.

### Pair comparison setelah kedua job native selesai

```bash
python3 claw/experimental/llvm_cpp/abi_oracle/compare_pair.py \
  --linux <artifact-linux>/evidence.json \
  --windows <artifact-windows>/evidence.json \
  --output paired-evidence.json
```

Comparator menolak SHA, source hash, runtime hash atau expectation corpus berbeda; memeriksa ledger O0/O2, penolakan kedua mode adapter, raw output dengan translasi newline CRT Windows yang eksplisit, serta perbedaan observed Str parameter SysV/Win64. Ia tidak menjalankan atau mengemulasikan Windows di Linux.

Fixture `borrowed_ref(value: Str) -> Str` memang memakai source `Str`; checker produksi mengkanonisasi parameter/return LIR menjadi **`ref Str [direct]`**. Dedicated borrowed-view helper juga demikian. Marker regression menggunakan klasifikasi aktual ini; `direct` pada LIR tidak otomatis berarti parameter LLVM aggregate pada Windows.
