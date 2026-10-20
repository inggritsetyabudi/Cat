# Migrasi native LLVM Cat

Migrasi dikerjakan pada `work/v0.1-v0.3-llvm-migration-20261020`, dari baseline `74fab2e39e301a25b4a6ff97fa7d84f898e6fce1`. `main` bukan tempat eksperimen. Tidak ada keputusan merge, versi, tag, atau release dalam pekerjaan ini.

## Irisan pertama: Cat mengendalikan emisi object

Jalur opt-in ini adalah **jembatan transisi**, bukan backend direct-LIR yang sudah selesai:

```text
Cat source → parser → sema → ownership → OIR → LIR
  → emitter LLVM text Cat yang sudah ada
  → LLVM 23.1.3 di dalam proses: parse → verify → DataLayout
  → O0 atau pipeline LLVM O2 → native ELF object
  → clang eksternal: kompilasi runtime C + link executable
```

Cat memilih target, PIC, optimasi, lokasi object, verifikasi, dan publikasi object melalui API LLVM. Clang tidak lagi menerima `.ll` pada jalur ini. Akan tetapi pembuatan LLVM IR masih memakai emitter teks Cat, runtime masih C, dan linking masih memakai driver Clang. Menyebut ini migrasi penuh atau penghapusan Clang adalah keliru.

Backend default tetap `clang`, termasuk pada compiler yang memiliki dukungan object. Tidak ada fallback diam-diam ketika pengguna memilih backend LLVM object yang tidak tersedia atau ketika parsing/verifikasi/emisi gagal. Semantik, sintaks, dan batas keselamatan Cat tidak berubah. Profil `debug` di sini memilih O0; ini **bukan** implementasi lengkap defect overflow debug dalam PRD.

## Build dan penggunaan

Build default tidak membutuhkan SDK LLVM. Jalur object memerlukan LLVM **tepat 23.1.3**, komponen static dengan header yang sesuai, dan host Linux x86_64 GNU. Windows, macOS, AArch64, dan cross-compilation belum diterima untuk irisan ini. Tidak ada fallback ke LLVM lain dari PATH.

```bash
cmake -S claw -B <build-object> -DCMAKE_BUILD_TYPE=Release \
  -DCLAW_ENABLE_LLVM_OBJECT=ON \
  -DCLAW_LLVM_CONFIG=/absolute/path/to/llvm23-sdk/bin/llvm-config
cmake --build <build-object> -j2
<build-object>/claw build --native-backend=llvm-object app.cat out.exe
<build-object>/claw build --release --native-backend=llvm-object app.cat out.exe
```

Artefak yang dipertahankan: `out.ll` berisi IR terverifikasi sebelum optimasi beserta DataLayout; `out.exe.cat.o` berisi object; `out.exe` berisi executable hasil link. Metadata SDK disimpan sebagai `native-object-sdk.txt` pada direktori build. Jika emisi gagal, IR awal tetap dapat diperiksa. Object lama tidak ditimpa sebelum object baru selesai dibangkitkan dan ditulis pada staging file satu filesystem; staging yang gagal dibersihkan. Ini bukan transaksi atomik atas ketiga artefak atau perbaikan transaksi output jalur linker lama.

Bangun satu output secara serial. Dua proses dengan output sama belum memiliki locking/isolasi transaksi; jangan memakai jalur ini untuk concurrent same-output builds. Kegagalan link mempertahankan IR/object untuk diagnosis dan tidak boleh dilaporkan sukses, tetapi pelestarian executable lama oleh linker eksternal belum dijamin. Kedua hal ini tetap gap pengerasan, bukan Safe gate yang sudah ditutup.

Pemisahan modul: `codegen/native_object.*` hanya mengurus LLVM dan object; driver mengatur tahapan native serta linking; `cmake/NativeObject.cmake` menjaga kontrak SDK. Tipe/header LLVM tidak menjadi API publik frontend Cat.

## Bukti yang wajib diperiksa

```bash
ctest --test-dir <build-object> --output-on-failure
python3 claw/tests/native/run_llvm_object_tests.py \
  --compiler <build-object>/claw --baseline <baseline-main>/claw \
  --disabled-compiler <build-default>/claw --artifacts <evidence-directory>
CLAW_EXE=<build-default>/claw bash claw/tests/run_all_tests.sh
```

Tes API memeriksa object ELF O0/O2, parser, verifier, target, DataLayout, pelestarian object lama, dan pembersihan staging gagal. Tes native menguji output exact pada fixture Cat, kedua profil, alias, default legacy, compiler tanpa dukungan object, penolakan semantik, path dengan spasi/metakarakter, dan spy argv Clang yang harus menerima object, bukan `.ll`. Tes tersebut bukan bukti seluruh subset PRD atau ABI lintas platform.

## Urutan lanjutan dan pengaman merge

1. Selesaikan bukti jembatan object dan jalur default pada snapshot sama; perluas matriks fitur Cat, ABI runtime, target sanitizer, serta kegagalan emisi/link. Pastikan fixture Safe tetap ditolak sebelum artefak native.
2. Ganti emitter teks dengan konstruksi LLVM API langsung dari LIR Cat secara modular. Admission per irisan harus eksplisit; tidak boleh mengubah identitas binding, cleanup, kontrol alur, atau semantik agar sesuai kemudahan backend. Perbandingan output dan diagnostik dengan baseline tetap wajib.
3. Ambil alih kompilasi runtime dan perencanaan linker/platform. Pilihan LLD/driver linker memerlukan bukti startup, CRT, simbol, dependensi, diagnostics, dan failure behavior; object-only tidak menyelesaikannya.
4. Tutup gap v0.1–v0.3 menurut [matriks penerimaan](v0.1-v0.3-acceptance-matrix.md) dan tiga otoritas `cat-northstar`, bukan dari jumlah tes yang hijau. Koleksi/pattern/slicing yang belum lengkap tetap belum lengkap.
5. Sebelum mempertimbangkan merge: review independen; full suite Release dan sanitizer compiler/target; Linux dan Windows pada SHA sama; bukti Fast/Safe/Simple; tidak ada regresi native/CLI; seluruh blocker terbuka diklasifikasi jujur. Merge tetap menunggu kematangan, bukan otomatis setelah irisan pertama.

PR #36 hanya referensi eksperimen. Kegagalan stack Windows `0xC0000005` versus `0xC00000FD` belum terbukti penyebabnya dan tidak diselesaikan oleh jalur Linux ini. PR #37 tidak menjadi basis kode atau otoritas yang sudah digabungkan. Arena self-reference dan borrowed ordinary shape fields tetap ditunda; tidak disentuh. Ledger PRD tidak dinaikkan labelnya oleh tersedianya opsi backend ini.
