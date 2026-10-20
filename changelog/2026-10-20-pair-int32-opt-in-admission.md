# Pair Int32 internal: admission opt-in terpisah

Tambahan eksperimen adapter LLVM C++ untuk exact `Tuple[Int32, Int32]`, melalui flag terpisah `--pair-i32` yang default false. Backend, runtime, ownership, ABI produksi, versi dan marker PRD tidak berubah. PR36 tetap draft dan tidak di-merge.

Validator typed LIR pair berdiri sendiri. Dua field per immutable stack binding wajib terbukti initialized sebelum whole-value read/transfer/direct call/return/projection. Missing/duplicate field, type/ABI/linkage/call mismatch, parameter/rvalue writes, unknown storage/instruction, branch/join/phi, owned/drop/provenance ditolak sebelum mengganti artefak. Tidak ada zero/undef/poison untuk partial initialization.

Pair tidak mengaktifkan arithmetic. Penjumlahan Int32 di fixture `take` hanya terbuka dengan flag `--arithmetic-i32` eksplisit bersamaan flag pair. Singleton validator dan corpus lama tidak diubah. TargetMachine native exact23.1.3 memeriksa layout size/alloc/store8, ABI align4, offsets0/4, little-endian, pointer64 dan address spaces270/271/272=32/32/64; preferred alignment8 dipisahkan.

Gate baru meliputi malformed LIR, flag isolation, sentinel/absent output dan cleanup temporary, sumber Cat nyata, produksi O0/O2 dan object adapter → external LLD23.1.3 → native exit42/stdout/stderr. Workflow pair native khusus ditambahkan bersama implementasi; full Release, host sanitizer dan Windows UCRT gates lama tetap dijalankan di runner disposable. Resource lokal tidak cukup untuk clean build penuh, sehingga source-only checks bukan bukti compile/native PASS.

Status penerimaan harus mengacu ke exact-head CI dan review independen, bukan keberadaan kode ini. Immutable stack source guard tetap memakai pin tree adapter lengkap dan blob workflow/test spesifik; tidak ada blanket exception subtree produksi/test. AV historis `0xC0000005` attempt1 belum terselesaikan; Windows sanitizer tetap SKIP. Arena dan borrowed fields ditunda.

## Koreksi gate setelah CI pertama

Exact head `9f039837d14f81e331d611d2f06207dde65cae59` berhasil dikompilasi,
namun gate pair gagal karena asumsi tekstual GEP field0; IR aktual LLVM23 memakai
bitcast base tanpa offset. Koreksi memeriksa alias/store field0 dan GEP/store
field1 serta projection index0/index1 secara struktural, bukan melonggarkan
admission atau mengubah emitter. Proof layout sebelumnya juga masih membekukan
tree adapter c5; kini source produksi tetap dibandingkan terhadap base lama,
sementara tree adapter diperiksa memakai pin tree lengkap dan provenance manifest
exact. Lokasi upload artefak native diperbaiki. Ini bukan klaim CI lulus; seluruh
gate harus dijalankan ulang pada head final.

Audit label O0/O2 adapter menemukan kedua run awal memakai TargetMachine default
sama. Gate diperbaiki dengan profile machine-code pair-only eksplisit None/Default
(`--pair-codegen-O0`/`--pair-codegen-O2`), bukan sekadar nama direktori. Tidak ada
pipeline IR debug/release baru; profile default -1 mempertahankan perilaku scalar
atau singleton lama dan profile tanpa pair ditolak sebelum output.

Pada `cb64bfa`, seluruh admission/native pair dan layout Linux+Windows serta PoC
Release/host-sanitizer/Windows lulus. Job pair Windows kemudian gagal saat
konfigurasi baseline: Git MSYS menghasilkan pointer worktree yang tidak terbaca
Git native CMake. Workflow menggunakan Git native saat membuat baseline, tanpa
mengubah source baseline atau menonaktifkan verifikasi revisi. Job PTY terpisah
berhenti exit127 tanpa diagnostik cukup; tidak ada assertion/source PTY diubah.
