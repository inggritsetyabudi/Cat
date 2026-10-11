# 2026-10-12 — identitas binding untuk local shadowing biasa

## Keputusan semantik

Shadowing nama lokal di **scope leksikal turunan** sah pada implementasi semantik saat ini. `Scope::define` menolak nama yang sudah ada pada scope yang sama, sementara `Scope::lookup` mencari scope parent; tidak ditemukan aturan PRD yang melarang shadowing. Deklarasi duplikat di scope yang sama tetap ditolak. PRD kanonis tidak diubah karena ini memperbaiki lowering untuk perilaku yang sudah diterima sema, bukan menambah sintaks atau mengubah keputusan bahasa.

## Perubahan

- Binding `val`/`var` yang men-shadow binding terlihat memperoleh key internal unik saat resolusi sema. Nama sumber tetap dipakai untuk diagnostik dan tampilan IR.
- Identitas diteruskan melalui AST/sema → OIR → LIR → LLVM untuk lookup lokal serta target assignment. Tuple destructuring juga menyimpan key per elemen.
- LLVM memakai key untuk memetakan storage; nama register alloca lokal dideduplikasi agar deklarasi bernama sama di scope saudara tidak menghasilkan definisi LLVM duplikat.
- Key cleanup milik ikut dibawa ke instruksi drop supaya shadowed binding dibersihkan melalui slot yang benar. Runtime dispatch drop yang sudah ada tetap sama.
- Key `Range[Int32]` scan yang sudah ada tetap terpisah dan tidak diganti. Tidak ada aturan, runtime contract, fixture, atau artifact Arena yang diubah; plumbing binding/drop ini menggunakan dispatch existing.

## Regresi

`claw/tests/native/fixtures/revise_local_shadowing_binding_identity.cat` mencakup nested branch (termasuk mutasi), tuple destructuring, cleanup scoped `Anchor`, ordinary local di dalam scan, serta scan yang men-shadow local lalu kembali ke local setelah loop. Frontend runner memeriksa program diterima; native runner mengunci output:

```text
8
3
8
99
70
110
7
5
99
1
2
88
99
```

Kasus repro minimal yang sebelumnya `claw check`-nya lolos tetapi native build gagal kini berjalan dan mencetak `7` lalu `99`. Regresi nested scan tiga level yang sudah ada juga tetap masuk ke native suite.

Sebelum perbaikan, repro yang sama dijalankan pada snapshot working baseline dan clean commit `1ffc95c`: keduanya menerima `claw check` (status 0), lalu gagal saat native build dengan LLVM `multiple definition of local value named 'item.addr'` (status 1).

## Validasi

Fresh configure/build dan seluruh test entrypoint dijalankan di salinan disposable `/tmp/cat-shadow-verified`:

```sh
cmake -S /tmp/cat-shadow-verified/claw -B /tmp/cat-shadow-verified/claw/build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cat-shadow-verified/claw/build --target claw -j2
CLAW_EXE=/tmp/cat-shadow-verified/claw/build/claw bash /tmp/cat-shadow-verified/claw/tests/run_all_tests.sh
```

Hasil: PRD doc-test, frontend, OIR formatter golden, backend LLVM, dan native executable suites lulus. Doc-test melaporkan 17 fence terklasifikasi; 2 blok executable diperiksa (1 lulus, 1 penolakan yang diharapkan), sedangkan blok proposal/text tetap dilewati sesuai klasifikasi.

Repro minimal diverifikasi dengan `claw check` dan `claw build`, lalu output dibandingkan tepat terhadap `7\n99`. Fixture baru juga dibangun dari LLVM IR dengan `clang -fsanitize=address,undefined -O1 -g` dan dijalankan memakai `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` serta `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`; output 13 baris cocok persis dan tidak ada diagnostik sanitizer.

`git diff --check` dan `bash -n` pada kedua test runner diperiksa terpisah sebelum pekerjaan dinyatakan selesai. Tidak ada stage, commit, push, atau PR yang dibuat.
