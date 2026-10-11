# 2026-10-12 — identitas receiver untuk bounds check `Str`

## Masalah yang direproduksi

Pada scope bertingkat, `text: Str = ""` di luar dapat di-shadow oleh `text: Str = "Y"` di dalam. Pemanggilan `text.first_byte()` sudah membawa binding key receiver pada jalur call, tetapi `LirBoundsCheck` hanya menyimpan nama sumber. LLVM lalu menghitung panjang dari `text` luar yang kosong dan trap dengan status 132, sebelum call memakai binding `text` dalam.

Baseline direproduksi pada build bersih:

```bash
cmake -S claw -B /tmp/cat-lirbounds-baseline -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cat-lirbounds-baseline --target claw -j4
/tmp/cat-lirbounds-baseline/claw check /tmp/cat-shadowed-str-bounds.cat
/tmp/cat-lirbounds-baseline/claw build /tmp/cat-shadowed-str-bounds.cat /tmp/cat-shadowed-str-bounds-baseline
/tmp/cat-shadowed-str-bounds-baseline.exe
```

`check` dan `build` berhasil; executable baseline trap dengan status **132**. LLVM baseline mengambil length dari alamat `text` luar untuk bounds check, tetapi mengambil byte dari binding dalam.

## Perubahan

- `LirBoundsCheckInst` kini membawa receiver `LirValue` opsional: teks, tipe, dan binding key diteruskan dari receiver builtin OIR. LLVM memakai nilai beridentitas tersebut untuk membaca panjang; jalur fallback lama tetap ada bagi instruksi tanpa metadata receiver.
- Nama `subject` tetap disimpan dan dipakai dalam representasi LIR serta diagnostik; semantik pemeriksaan tidak berubah.
- Regresi native `revise_str_bounds_shadowing.cat` mengunci hasil shadowed `first_byte`, `last_byte`, `byte_at`, dan `slice`: `89\n89\n89\nY`.
- Regresi `revise_try_result_shadowing.cat` juga mengunci binding hasil sukses serta binder error `try` yang men-shadow `err` luar: `7\n99\ninner\n0`.
- `revise_try_owned_result.cat` diperluas dengan `Anchor` owned hasil `try` yang men-shadow nilai luar, mencakup jalur sukses, gagal, cleanup scope, dan return awal. Golden OIR diperbarui.
- `revise_pick_anchor_payload_shadowing.cat` diperluas dengan payload `Anchor` pada ekspresi `pick` yang men-shadow nilai luar, tanpa menggandakan cakupan statement `pick` yang sudah ada. Output native wajib `inner\nouter\ninner\nouter`.

## Validasi

- Build compiler bersih dan seluruh suite (doc, frontend, OIR, backend LLVM, native) lulus pada salinan disposable setelah pembaruan golden.
- Regresi native try-owned dijalankan dengan `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`; output cocok dan tidak ada laporan ASan/LSan.
- Regresi pick-owned dijalankan dengan ASan/UBSan/LSan, `detect_leaks=1`, dan `LSAN_OPTIONS=exitcode=23`; output cocok dan tidak ada laporan sanitizer.
- `bash -n` untuk seluruh runner shell dan `git diff --check` lulus.

## Batas

Tidak ada perubahan implementasi, kontrak, fixture, atau artifact Arena. PRD kanonis tidak diubah; status bagian terkait tetap parsial. Tidak ada staging, commit, push, atau PR.
