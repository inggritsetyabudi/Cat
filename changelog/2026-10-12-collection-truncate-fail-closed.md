# 2026-10-12 — fail-closed untuk `Vec.truncate` dan `Queue.truncate`

## Dasar kontrak

`cat-northstar/PRD.md` §2 menetapkan bentuk literal koleksi dan representasi `Vec` (`{ ptr, len, cap }`), tetapi tidak menetapkan kontrak `truncate`: aturan batas panjang, perlakuan atas elemen yang dihapus, atau penghancuran nilai owned. Karena `Vec[T]` dapat membawa elemen owned, implementasi tanpa drop glue yang aman akan mengarang perilaku ownership. PRD hanya menyebut `Queue.new()` sebagai bentuk sementara sampai bentuk literalnya ditentukan; representasi/storage dan lowering native `Queue` juga belum tersedia. Audit [builtin-methods.md](../claw/docs/builtin-methods.md) sebelumnya menyatakan metode koleksi ini belum end-to-end.

## Perilaku sebelum perubahan

- Semantik mendaftarkan `Vec.truncate(USize) -> Unit` dan `Queue.truncate(USize) -> Unit`, sehingga `claw check` menerima panggilan yang tampak valid.
- LIR menandainya sebagai bounds check, tetapi LLVM lowering tidak mempunyai kasus `truncate` maupun implementasi native-nya. Repro sebelum perubahan lulus `check` lalu gagal pada build dengan `LLVM lowering does not yet support bounds_check kind 'truncate'`.

## Perubahan

- Menghapus registrasi semantik `Vec.truncate` dan `Queue.truncate`; panggilan kini ditolak saat frontend dengan diagnostik tipe yang tidak menyediakan metode tersebut.
- Menghapus pengenalan serta klasifikasi bounds-check LIR yang khusus untuk `truncate`, agar tidak tersisa jalur builtin yang tidak dapat dicapai melalui kontrak semantik.
- Menambah regresi frontend untuk `Vec.truncate` pada receiver parameter dan alias receiver yang di-shadow di scope bertingkat; keduanya harus ditolak dengan diagnostik semantik yang sama. Regresi terpisah memastikan `Queue.truncate` ditolak.
- Memperbarui audit metode pada `claw/docs/builtin-methods.md` dan mengindeks catatan ini. PRD kanonis tidak diubah.

## Validasi

Di salinan disposable dengan build Debug baru:

```bash
cmake -S "$REVIEW_DIR/claw" -B "$REVIEW_DIR/claw/build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$REVIEW_DIR/claw/build" --target claw -j4
CLAW_EXE="$REVIEW_DIR/claw/build/claw" bash "$REVIEW_DIR/claw/tests/run_all_tests.sh"
find "$REVIEW_DIR/claw/tests" -type f -name '*.sh' -print0 | xargs -0 -n1 bash -n
git diff --check
```

- Build compiler bersih lulus.
- Probe frontend `Vec.truncate` (termasuk receiver inner yang men-shadow receiver luar) dan `Queue.truncate` masing-masing gagal tepat dengan `Type '<type>' does not provide method 'truncate'.` tanpa diagnostik lain.
- Seluruh suite doc-test, frontend, OIR, backend LLVM, dan native lulus; sanitizer cases yang sudah ada di native suite juga lulus.
- `bash -n` untuk semua runner shell dan `git diff --check` lulus. Tidak ada perubahan ownership/drop implementation, jadi tidak dibuat sanitizer test untuk `truncate` yang memang belum didukung.

## Batas dan status

`Vec.truncate` dan `Queue.truncate` tetap tidak didukung. Membukanya kembali memerlukan kontrak bounds/destruction yang eksplisit beserta implementasi storage, drop, lowering, dan native yang lengkap. Tidak ada milestone yang ditandai selesai. Tidak ada file Arena atau artifact Arena yang diubah dalam follow-up ini; tidak ada staging, commit, push, atau PR.

Catatan lokasi: build, probe, seluruh suite, dan `bash -n` dijalankan di `/tmp/cat-truncate-review-final.CdLFc6`; `git diff --check` dijalankan pada checkout sumber di `/workspace/Cat-modularity-20261011`.
