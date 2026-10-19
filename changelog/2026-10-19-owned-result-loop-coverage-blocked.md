# Cakupan komposisi owned Result/try/pick/loop — terblokir

## Lingkup

Baseline yang diverifikasi: `f8816eb6a8addcf70801bc5fc3469f1eecf4bab6`. Slogan tetap **Fast, Safe, Simple**. Perubahan ini hanya fixture, registrasi tes, dan dokumentasi bukti; tidak mengubah compiler, runtime produksi, keputusan semantik, atau marker PRD/G2.

`composition_owned_result_loops.cat` memakai sintaks yang sudah ada: owned `Result[Step, String]`, initializer `val ... = try`, `pick` di dalam `scan` dan `loop`, payload `String`, serta `skip`/`stop`/early return. Empat panggilan mencakup sukses dan gagal; stdout persis memeriksa jalur yang dijalankan, kelanjutan sesudah cabang/fungsi, dan absennya `unreachable-loop-tail`.

## Defect yang ditemukan

Pemeriksaan dan native execution biasa memberikan output yang diharapkan. Target ASan/UBSan/LSan menemukan **10 byte bocor dalam satu alokasi**, yaitu lokal `String.from("scan-guard")` saat shorthand `try` meneruskan `Fail`. Repro lebih kecil membuktikan loop dan `pick` bukan syarat kebocoran:

- `claw/tests/integration/repros/try_propagation_cleanup.cat`: lokal owned hidup sebelum shorthand `try`, kemudian propagasi gagal; tidak memerlukan loop.
- `claw/tests/integration/repros/try_scan_cleanup.cat`: mempertahankan konteks `scan` dan menghasilkan defect sama.

Kedua repro mencetak `failure\nafter\n`, tetapi target terinstrumentasi keluar 1 dengan `SUMMARY: AddressSanitizer: 10 byte(s) leaked in 1 allocation(s).` pada O0 dan O2. LLVM jalur `try.fail` meneruskan payload error dan return tanpa memanggil `claw.runtime.string.free` untuk lokal guard. Kontrol eksplorasi `try ... else error { return Fail(error) }` pada O0 tidak menunjukkan leak; regresi asli **tidak** diganti dengan kontrol.

## Validasi dan batas

- Compiler baseline dibangun dengan Clang Release dalam clone disposable; `check`, native build, dan output biasa fixture komposit lulus.
- Kedua repro: native build lulus; target ASan/UBSan/LSan O0 dan O2 gagal dengan leak 10 byte tersebut.
- Fixture baru tetap terdaftar di runner komposisi; persyaratan sanitizer dan stdout tidak dilemahkan, tidak diberi expected-failure, dan tidak dilewati.
- CI/head PR dan hasil review independen harus dibaca dari PR terkait; catatan ini tidak mengklaim seluruh suite atau CI lulus.
- Penambahan pemanggilan fungsi terminal `if`/`pick` ditunda karena defect sudah memblokir workstream. Bukti cleanup positif yang dapat diamati juga belum ditutup. Assertion terstruktur AST/AIR/OIR/LIR per fixture tetap pekerjaan terpisah.

Perbaikan compiler harus menjadi workstream bugfix terpisah. PR cakupan ini tidak siap digabung sampai defect diperbaiki dan seluruh gerbang yang relevan divalidasi ulang. Tidak ada klaim bahwa v0.1–v0.3 matang atau bahwa compiler bebas semua bug.
