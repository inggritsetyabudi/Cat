# Changelog — integer division/remainder: zero divisor dan overflow (2026-10-18)

## Baseline compiler sebelum perubahan

Branch khusus `fix/integer-division-by-zero` dibuat dari `main` terbaru yang diverifikasi melalui GitHub, `e7789c6efedff677b81c2f883fe8498e32c30554`. PR #22 tetap independen dan tidak disentuh.

Dokumen keputusan yang dibaca: `cat-northstar/PRD.md`, `PRD-CHANGES-v0.1-to-v1.0.md`, `ROADMAP-v1-plus.md`, dan `SPEC.md`. Path historis `revise/cat-language-design-summary_all.md` kini tersedia sebagai `cat-northstar/legacy/cat-language-design-summary_all.md`; dokumen legacy tidak menggantikan keputusan canonical.

Compiler baseline dibangun dari source dengan Clang dalam build Release baru. Probe sungguhan melalui `claw build` kemudian menjalankan executable native `-O2`: fungsi pembagian dengan denominator nol dan ekspresi literal `42 / 0` sama-sama berhasil dibangun, mencetak `1`, lalu keluar dengan status `0` tanpa diagnostik. `% 0`, `-2147483648 / -1`, dan `-2147483648 % -1` memperlihatkan hasil baseline yang sama. Ini bukan hasil bahasa yang dijamin: LLVM semula menerima operasi integer yang tidak terdefinisi/poison.

Static initializer berupa `42 / 0` sudah ditolak dengan `Static variable initializer must be a compile-time literal expression.` Tidak ada evaluator konstanta global umum yang ditambahkan.

## Perubahan pada irisan ini

- Semua operasi builtin integer `/`, `%`, `/=`, dan `%=` yang didukung bertemu pada lowering binary LLVM yang sama. Divisor dievaluasi sekali, lalu diperiksa sebelum operasi integer.
- Divisor nol memanggil helper runtime `noreturn`, menulis `fatal: integer division or remainder by zero; divisor must be nonzero` beserta newline ke stderr, melakukan flush stderr, lalu `abort()`. Tidak ada nilai hasil atau kelanjutan eksekusi. Status proses nonzero; angka exit/signal berbeda antarplatform. Defect ini tidak melakukan unwind atau menjanjikan destructor cleanup.
- Guard berlaku pada `Int8`, `Int16`, `Int32`, `Int64`, `Int128`, `UInt8`, `UInt16`, `UInt32`, `UInt64`, `UInt128`, `USize`, dan alias `Int`/`UInt`, termasuk RHS literal atau konstanta lokal yang dieksekusi.
- Untuk target native release yang tersedia, pasangan signed `MIN` dan `-1` menghasilkan quotient wrapping `MIN` atau remainder `0`. Divisor aman dipilih **sebelum** `sdiv`/`srem`, kemudian hasil wrapping dipilih secara eksplisit. Zero divisor dan pasangan overflow tidak pernah menjadi operand invalid pada instruksi division/remainder LLVM, bahkan pada IR statis yang tidak dieksekusi.
- Jalur float tetap memakai `fdiv`, tanpa guard integer atau perubahan ke flag fast-math. `Float32`/`Float64` mempertahankan aritmetika valid dan perilaku IEEE-754 untuk pembagian nol.
- Evaluasi short-circuit dan cabang yang tidak diambil tetap tidak menjalankan operasi di dalamnya. Ordinary nonzero arithmetic, signed truncation, dan hasil unsigned tidak diubah.

## Dasar kontrak dan batas profil

PRD canonical menetapkan overflow signed sebagai defect pada debug dan wrapping pada release (`PRD.md` ringkasan, §6, dan keputusan penutup; change plan §19/v0.8). Karena itu `MIN / -1` **tidak** diubah menjadi trap tanpa syarat. Remainder pasangan tersebut didefinisikan sebagai `0`.

Compiler saat ini hanya menyediakan target native `clang -O2`, tanpa profil target debug atau flag pemilih profil (`claw/README.md`, bagian CLI; `native_build.cpp`). Build CMake Debug menginstrumentasi/compiler host, bukan mengganti semantik program Cat. Relink IR pada `-O0` juga bukan profil debug Cat. Persyaratan debug-overflow tetap staged bersama profil target tersebut; perubahan ini tidak menambahkan sistem profil, unwind, stack trace, atau mengklaim milestone v0.8 selesai.

Operator custom, static initializer umum, dukungan backend `Float128`, Arena, partial moves, borrowed fields, dan f-string tidak diperluas. Fixed-array indexed compound assignment tetap ditolak; indexed compound yang diuji mengikuti subset mutable `Span[Int32]` yang sudah didukung.

## Pengujian

Runner terintegrasi: `claw/tests/native/run_division_by_zero_tests.py`, dipanggil oleh suite native. Source, IR, executable, dan hasil terkait dipertahankan dalam direktori artifact native untuk inspeksi.

- Matriks meliputi 13 tipe/alias integer, 152 fungsi dinamis yang dipanggil melalui C ABI terpisah, dan 35 kasus ekspresi. Satu eksekusi Linux lengkap mencatat **1.145 run sukses, 530 zero-divisor trap dengan stderr persis, dan empat penolakan batas sintaks yang dipertahankan**.
- Seluruh kasus dinamis dieksekusi pada link `-O0`, `-O2`, `-O3`, serta ASan/UBSan/LSan; bukan hanya berhasil dikompilasi. Literal/computed/local/compound zero dan hasil literal `MIN/-1` juga dibangun lewat CLI native. Signed truncation, unsigned high-bit/max values, numerator nol, dan tetangga batas signed diperiksa hasilnya.
- Jalur ekspresi meliputi return/tail, argumen dan arithmetic bersarang, kedua cabang `if`/`pick`, short-circuit skipped/evaluated, loop condition/body, scan, tuple, field, array Int32/UInt8, plain indexed store, serta mutable `Span[Int32]` `/=` dan `%=`. Counter mengamati bahwa indeks dan RHS hanya dievaluasi sekali. Wrapper Cat mempertahankan detail ABI choice/Bool di dalam Cat; harness Span mengikuti ABI pointer Windows dan value SysV yang sudah ada.
- Float32/Float64/Float diuji dengan C `isinf`/`isnan`/`signbit`, termasuk pembagian positif/negatif, NaN, signed zero, compound assignment, dan ordinary nonzero results. Pengujian tidak bergantung pada ejaan teks NaN antarplatform.
- **Clean disposable Linux Clang Release build + seluruh `run_all_tests.sh`: lulus.**
- **Seluruh suite pada salinan terpisah dengan compiler host ASan/UBSan dan leak detection: lulus**, termasuk matriks native sanitizer di atas. Ini bukan klaim tersedianya profil target debug.
- Review independen atas lowering/runtime dan snapshot final 35-case fixture/harness/runner: **tidak ada blocker**. Reviewer juga memverifikasi IR dengan LLVM verifier pada representasi lebar kecil, Int128, UInt128, dan USize.
- Workflow CI yang sudah ada menjalankan suite penuh untuk Linux Clang Release dan Windows MSYS2 UCRT64 Clang Release pada PR. Status aktualnya diperiksa pada head PR; tidak ada perubahan workflow, merge, tag, version bump, atau release dalam irisan ini.
