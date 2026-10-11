# Identitas binding pada nested `Range[Int32]` scan (2026-10-12)

Nested `scan` yang memakai ulang nama binding kini mempertahankan referensi ke scope yang benar setelah loop dalam selesai. PRD kanonis tidak diubah; tidak ada perubahan pada Arena.

## Temuan baseline

Program nested `scan item, index` lolos `claw check` dan native build, tetapi sesudah inner loop membaca ulang outer binding menghasilkan nilai inner terakhir (`2, 1`) alih-alih nilai outer saat itu (`20, 0` lalu `21, 1`). LLVM menyimpan alamat variabel dengan nama sumber mentah dan inner scan menimpa alamat outer tanpa memulihkannya.

## Perubahan

- Semantik memberi key internal unik pada setiap binding item/index scan; identifier menyimpan key simbol yang benar hasil resolusi scope.
- Key binding diteruskan secara terpisah dari nama sumber melalui OIR dan LIR, sehingga tampilan IR dan variable lokal umum tetap memakai nama sumber.
- Backend LLVM mendaftarkan dan mencari slot item/index melalui identitas binding, bukan nama mentah. Jalur tanpa metadata tetap menggunakan nama lama.

## Regresi dan verifikasi

`claw/tests/native/fixtures/revise_int32_range_scan_shadowing.cat` menambahkan nested scan tiga level: inner scan men-shadow `item` dan `index`, scan terdalam kembali men-shadow `item`, lalu fixture membaca ulang binding inner dan outer. Harness mengunci seluruh output, termasuk outer `20, 0` dan `21, 1` setelah scope inner berakhir. Fixture khusus ini tidak mengubah golden OIR scan yang sudah ada.

Clean configure/build pada `/tmp/cat-shadow-verified` lulus dengan:

```sh
cmake -S /tmp/cat-shadow-verified/claw -B /tmp/cat-shadow-verified/claw/build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cat-shadow-verified/claw/build --target claw -j2
CLAW_EXE=/tmp/cat-shadow-verified/claw/build/claw bash /tmp/cat-shadow-verified/claw/tests/run_all_tests.sh
```

Seluruh PRD doc-test, frontend, OIR golden, backend LLVM, native executable, dan sanitizer suites lulus. `claw check` dan `claw build` langsung atas fixture baru juga lulus; native stdout persis cocok dengan expected output. Eksekusi fixture yang dikompilasi dengan `-fsanitize=address,undefined` dan `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` juga lulus tanpa temuan. `git diff --check` dan `bash -n claw/tests/native/run_native_tests.sh` lulus.
