# 2026-10-20 — Exact pair Int32 production native roundtrip

Menambahkan satu fixture produksi `revise_pair_i32_roundtrip.cat`: helper
`make(x: Int32, y: Int32) -> (Int32, Int32)` membangun dua field; helper
`take(p: (Int32, Int32)) -> Int32` membaca field0/1; `main` mengembalikan42.
Fixture tuple lama tetap utuh: roundtrip parameter di `revise_tuples.cat`
menggunakan Int64, sedangkan fixture destructuring Int32 tidak melakukan
parameter roundtrip exact beserta kontrak exit42/stdout/stderr O0/O2 ini.

Harness baru dipanggil dari runner native produksi, yang sudah dijalankan oleh
CI Linux Release, Linux host ASan/UBSan/LSan, dan Windows MSYS2 UCRT64 Release.
Satu source yang sama menjalani check/OIR/LIR/LLVM, `build` O0 dan
`build --release` O2, lalu executable ELF atau PE benar-benar dijalankan native.
Kedua executable harus exit42 dengan stdout dan stderr byte-exact kosong.
LIR aktual diwajibkan menunjukkan constructor field0/1 terinisialisasi sebelum
return dan pair parameter/result Direct, internal Cat ABI, `ffi=false`.

Signature/call diperiksa pada raw LLVM aktual setiap stage/build serta LLVM
setelah diproses Clang native O0. DataLayout efektif Clang dan triple kedua
module disimpan; vendor triple boleh dikanonisasi Clang, tetapi arsitektur,
OS dan environment target tidak boleh berubah. Tidak ada layout singleton
hardcoded, oracle C struct, klaim numeric pair layout equivalence terhadap
metadata frontend/LLVM SDK, atau klaim C ABI/FFI. DataLayout raw produksi yang
belum diterbitkan dicatat sebagai null, bukan direkayasa. Setiap run default
memiliki direktori artefak baru; output dan corpus lama tidak dihapus. CI
mengunggah command/results, LIR, raw/effective LLVM dan executable pair.

Validasi lokal terbatas lulus memakai compiler existing dari `6d697e8`;
tracked subtree `claw/src` dan `claw/runtime` identik terhadap branch ini.
Release dan host ASan/UBSan masing-masing menjalankan fixture O0/O2 native
Linux exit42 tanpa stdout/stderr. Ini bukan fresh exact-head compiler build
atau full-suite lokal, dan host sanitizer bukan instrumentasi executable Cat.
Percobaan harness pertama gagal karena membandingkan vendor triple Linux
`unknown` vs kanonisasi Clang `pc`; bukti gagal dipertahankan dan perbandingan
sekarang mempertahankan arsitektur/OS/environment tanpa menganggap vendor
berbeda sebagai target berbeda. Bukti Windows baru sah setelah CI exact-head
benar-benar mengeksekusi fixture di UCRT64, bukan setelah check/cross-compile.

Tidak ada edit compiler, runtime, adapter atau semantics. Pair admission tetap
CLOSED; tidak ada `--pair-i32`. Layout equivalence kedua target, malformed-LIR
pair admission/rejection dan object→LLD→native pair adapter matrix tetap belum
selesai. PR36 tetap draft/default OFF, tanpa merge, version bump, PRD marker
atau promosi maturity. Canonical `cat-northstar/` dan semua deferred semantics
tetap otoritatif. Windows sanitizer tetap SKIP, bukan PASS. Original stack
diagnostic attempt-1 AV `0xC0000005` tetap unresolved.
