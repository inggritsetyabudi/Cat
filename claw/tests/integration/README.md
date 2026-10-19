# Gerbang integrasi kombinasi v0.1–v0.3

`run_composition_tests.py` selalu dipanggil oleh `claw/tests/run_all_tests.sh`. Pengujian versi berikutnya harus tetap menjalankan gerbang ini: bukti lama dipertahankan sebagai regresi otomatis, bukan dianggap tidak perlu diuji lagi.

## Yang benar-benar dijalankan

Setiap fixture positif harus lolos `check`, emisi LLVM, native build, dan eksekusi dengan stdout persis. LLVM hasil native build wajib memiliki wrapper `@main`; LLVM tersebut kemudian dikompilasi ulang dengan Clang pada **O0 dan O2**, baik biasa maupun dengan **ASan/UBSan/LSan**. Dengan runtime sanitizer tersedia, empat fixture awal menghasilkan **20 eksekusi target**: native driver O2, relink O0/O2, dan relink O0/O2 bersanitizer. Fixture kelima menargetkan lima eksekusi tambahan, tetapi saat ini **terblokir oleh kebocoran yang direproduksi**; jangan melaporkan 25 eksekusi lulus. Artefak modul LLVM, LLVM executable, dan executable disimpan di `artifacts/` (tidak dimasukkan ke commit).

- `composition_core.cat`: fungsi dan pilihan nilai `if`/`pick`, `Result`/`Maybe`/`try`, array lintas panggilan dengan scan berindeks dan assignment, nested tuple destructuring, serta view string.
- `composition_workspace/`: manifest, import alias, shared function melalui modul saudara, choice, `if`/`pick`, dan range scan. Array lintas panggilan ada di `composition_core.cat`, bukan fixture workspace.
- `composition_ownership_ranges.cat`: owned `String` melalui pilihan, fungsi, dan choice; jalur gagal `Result` dengan cleanup; view dari owner yang tetap hidup; `get` UTF-8 valid/tidak valid; Char scan melintasi surrogate gap; range UInt8 inklusif sampai 255; `loop`/`skip`/`stop`.
- `composition_implicit_result.cat`: `try` dan implicit return `Ok(...)` di fungsi bertipe `Result`, kemudian consumer `pick`; regresi defect konteks tipe yang ditemukan saat penyusunan matriks.
- `composition_owned_result_loops.cat`: `Result[Step, String]` owned dibuka melalui `try`, kemudian `pick` di dalam `scan`/`loop`; success, failure, `skip`, `stop`, dan early return. Stdout persis menguji kelanjutan sesudah cabang dan absennya `unreachable-loop-tail`. Ini belum membuktikan cleanup: target LSan menemukan kebocoran 10 byte pada propagasi gagal shorthand `try`, sementara lokal `scan-guard` masih hidup. Pemeriksaan sanitizer tetap wajib dan menggagalkan runner; tidak ada expected-failure atau skip baru.

Dua companion **wajib ada**, bukan skip opsional:

1. `run_negative_composition_tests.py`: 13 fixture × `check`/OIR/LLVM/build = **52 pemeriksaan**. Tiap penolakan memerlukan pesan diagnostik lengkap yang cocok, lokasi file/baris/kolom, exit bukan nol, stdout kosong, dan tidak ada LLVM/executable. Termasuk shape borrowed field dan owned tuple projection/partial move yang belum didukung. `try_shorthand_unit_function.cat` menolak `val value = try ...` dalam fungsi Unit; fixture ini bukan pengujian syntax `return try`.
2. `run_pattern_oracle.py`: **100 kasus deterministik**, seed `0xC0FFEE`, dengan matcher struktur independen atas domain choice dan nested tuple terbatas (44 accepted, 56 rejected, 13 build/run native). Mencakup accepted, missing coverage, duplicate/unreachable, wildcard ordering dan catch-all ganda, serta build/run subset accepted dengan stdout persis. Ini bukan bukti matematis exhaustiveness untuk seluruh bahasa.

## Menjalankan

```bash
CLAW_EXE=/path/to/claw python3 claw/tests/integration/run_composition_tests.py
```

Compiler host bersanitizer diuji **terpisah** oleh job CI Linux `linux-sanitizers`, yang menjalankan seluruh suite dengan `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` dan `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Instrumentasi target tidak menggantikan instrumentasi compiler, dan sebaliknya.

Runtime sanitizer target wajib tersedia di Linux. Di platform lain, runner mencoba compile/run probe; hanya jika runtime tidak tersedia, runner menulis **SKIP** eksplisit berikut alasannya. O0/O2 biasa tetap wajib. Skip Windows tidak boleh dilaporkan sebagai sanitizer PASS.

## Repro terblokir: cleanup propagasi `try`

`repros/try_propagation_cleanup.cat` adalah repro lebih kecil tanpa loop atau choice buatan pengguna; `repros/try_scan_cleanup.cat` mempertahankan konteks `scan`. Keduanya mencetak `failure\nafter\n`, tetapi target ASan/UBSan/LSan pada O0 dan O2 keluar 1 dengan `10 byte(s) leaked in 1 allocation(s)`. Output benar tidak berarti cleanup benar. Kontrol eksplorasi yang mengganti shorthand menjadi `try ... else error { return Fail(error) }` tidak menunjukkan leak pada run O0 yang diuji; ini bukan alasan mengganti regresi dengan kontrol tersebut.

Contoh menjalankan repro terkecil dari root repo, dengan build compiler yang sudah tersedia:

```bash
"$CLAW_EXE" build claw/tests/integration/repros/try_propagation_cleanup.cat /tmp/try-cleanup.exe
clang -O0 -fsanitize=address,undefined,leak -fno-omit-frame-pointer -g \
  /tmp/try-cleanup.ll claw/runtime/native_runtime.c -o /tmp/try-cleanup.san.exe
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/try-cleanup.san.exe
```

Perbaikan compiler wajib menjadi workstream terpisah. Pemanggilan runtime fungsi terminal `if`/`pick` dan observasi cleanup positif tertunda; tidak ada acceptance PRD/G2 yang berubah.

## Batas penerimaan

Hanya syntax yang didukung masuk fixture positif. `String` memakai `as_str()` untuk slicing/get; owned choice hasil sementara diikat ke lokal sebelum `pick`; view dari owner berbeda tidak digabungkan; borrowed `Str` dalam tuple dan arbitrary owned aggregate bukan janji dukungan. Lihat [matriks penerimaan](../../../docs/v0.1-v0.3-acceptance-matrix.md) untuk pemisahan implementasi, proposal, dan batas implementasi yang tersisa setelah integrasi main.

Lulusnya gerbang ini berarti kasus yang disebutkan tidak menunjukkan regresi atau laporan sanitizer pada run tersebut. Ini **bukan** pernyataan seluruh PRD selesai atau compiler bebas semua bug kode/memori.
