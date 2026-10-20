# 2026-10-20 — Opt-in singleton Int32 internal pada adapter LLVM C++

## Perubahan yang sengaja dibatasi

Menambahkan `--singleton-i32` / `ObjectOptions.enableSingletonI32`, default **OFF**, terpisah dari local-binding keys, runtime, aritmetika, dan String. Slice hanya canonical `Tuple[Int32]`: constructor field0 Int32 ke immutable stack alloca, transfer utuh, projection `.0`, direct internal `abi=claw` call/return dengan ABI `Direct`. TargetMachine DataLayout harus membuktikan size4/align4/offset0; LLVM tetap nonpacked `{ i32 }`, bukan scalar flattening, C ABI, `sret`, atau `byval`.

Validator typed LIR dijalankan sebelum lowering dan publikasi object/IR. Tipe, signature, linkage, asal slot, identitas function-local, init-before-read, literal, field safety, dan provenance diperiksa fail-closed. Provenance tidak pernah menjadi operand/storage key. Parameter/rvalue tidak boleh menjadi target field store; provenance malformed/nonempty, owned drop, unknown instructions/storage dan duplicate IDs ditolak. CFG scope dibatasi ke rantai linear terurut dengan predecessor/goto persis; branch, cycle, join dan phi ditolak.

Destructuring nonowned `val (x,) = p` tidak dibedakan dari projection jika actual canonical LIR byte-identical. Wildcard nonowned tidak menambah runtime operation bila LIR byte-identical dengan unused parameter. Frontend/ownership produksi tetap menjadi penentu validitas source; tidak ada tag asal sintaks baru.

IR dump singleton menyimpan module API yang telah diverifikasi sebelum machine passes LLVM mengubah zero-offset GEP/stack alignment. Kebijakan dump flag OFF tetap lama. Publikasi masih menggunakan staging existing dan penolakan mempertahankan sentinel object/IR.

## Pengujian baru dan bukti awal

- `llvm-cpp-singleton-admission`: empat bentuk positif (keyed aggregate, source-shaped scalar, Unit, linear goto chain), lima konfigurasi flag OFF, dan **66 mutasi malformed LIR**, masing-masing dengan/ tanpa seluruh flag lama serta output absent/sentinel byte-exact. Gate lokal Linux Release lulus.
- `llvm-cpp-singleton-native`: tujuh source positif (canonical roundtrip, projection, equivalent destructuring, unused, wildcard, shadow, nested scope); produksi check/OIR/LIR/LLVM dan O0/O2; empat mode adapter, object → external LLD23.1.3 → native ELF dengan exit42/stdout/stderr kosong. Package runtime/archive existing tidak ditulis ulang, emisi dan final link berjalan tanpa compiler di PATH. Gate lokal Linux Release lulus.
- OOB field, wrong destructuring arity/type, dan use-after-destructure wajib ditolak semantic/ownership frontend, bukan parse failure. Pair/nested source tetap sah pada produksi tetapi adapter menolak di seluruh mode. Negative malformed LIR dan flag OFF tidak mempublikasikan output baru atau menimpa sentinel.
- Baseline produksi `24211867ed293bc4a501fadbf0d6d6961fce6768` dibandingkan terhadap build produksi baru untuk seluruh **500 tracked source Cat × check/OIR/LIR/LLVM = 2.000 stage comparisons**: exit/stdout/stderr identik.
- Seluruh corpus/oracle/hash ledger historis dipertahankan. `singleton-reviewed-deltas.json` mencatat perubahan tiga file backend dan empat file admission baru secara terpisah; pemeriksa audit identitas lama memverifikasi rantai delta tersebut tanpa mengubah corpus atau memberi akses singleton di audit String lama.

Regresi penuh Release, host ASan/UBSan, seluruh suite produksi dalam disposable copies, Windows UCRT native, exact-head CI, dan independent source/artifact review tetap menjadi gate berikutnya. Bukti awal di atas tidak mengklaim gate yang belum selesai sebagai PASS. Windows sanitizer tetap SKIP kecuali tool aktual tersedia; host sanitizer tidak berarti executable target/runtime C seluruhnya terinstrumentasi.

## Bukan promosi produksi

Tidak mengubah frontend, semantik/sintaks/ownership, runtime produksi, PRD, status fitur bahasa, atau versi. Pair/nested/generic aggregate, aggregate phi/join, Pick/Choice/Anchor, ref/view, owned cleanup, FFI/external ABI, serta arena self-reference/borrowed fields tetap di luar slice atau ditunda sesuai spesifikasi kanonik. Desain C@ tetap milik Cat, bukan penggantian nama desain bahasa lain.

PR #36 tetap **draft**, feature default **OFF**, tanpa merge/promotion. Kegagalan terminal Windows stack attempt1 access violation tetap **nyata dan belum terselesaikan**; exact rerun/prototype first-chance hijau tidak membuktikan perbaikannya dan tidak dikaitkan sebagai keberhasilan backend singleton.
