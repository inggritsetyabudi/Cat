# 2026-10-16 — Casing ASCII-insensitive untuk scalar builtin

## Perubahan

Normalisasi nama tipe kini menerima casing ASCII apa pun hanya untuk daftar scalar builtin tertutup: `Bool`, `Char`, `Unit`, `Int8`–`Int128`, `UInt8`–`UInt128`, `USize`, `Float32`–`Float128`, dan alias `Int`, `UInt`, `Float`. Hasil internal tetap canonical dan alias tetap memetakan ke `Int64`, `UInt64`, serta `Float64`. Nama tipe buatan pengguna dan constructor generic tidak dilipat secara umum; `Point`/`point` tetap berbeda, begitu juga `Array`/`array`, `Range`/`range`, dan `Vec`/`vec`.

Pengenalan suffix numeric memakai daftar scalar yang sama, dengan normalisasi hasil ke nama canonical. Pemisah radix tetap benar: `0xFF_F` dan `0xAB_cD` adalah digit/separator hex, sementara `_fLoAt32` adalah suffix. Tail suffix tak dikenal tetap error sesuai grammar yang ada. Aturan yang diubah di PRD hanya §4 dan §11; tidak ada section/milestone yang ditandai `[sudah]`.

## Batas dukungan yang ditemukan

`Float128` annotation casing berhasil dikanonisasi dan diverifikasi melalui AIR. Probe `1.0_float128` menunjukkan suffix dikenali, lalu ditolak oleh batas semantik yang sudah ada (suffix float literal saat ini hanya dimaterialisasi untuk `Float32`/`Float64`); backend LLVM juga belum memetakan `Float128`. Dukungan literal/backend Float128 tidak diperluas dalam perubahan casing ini.

## Verifikasi

Compiler bersih yang dibangun sebelum perubahan menolak fixture reproduksi dengan dua error `Unknown type` (`int32`, `float32`) serta dua error `Malformed ... literal` (`7_int32`, `1.25_float32`); fixture yang sama lulus setelah perubahan. Focused frontend suite lulus (`exit 0`), dan native fixture baru berhasil dibangun serta menghasilkan semua 13 keluaran yang diharapkan. Full clean build dan seluruh suite (`run_all_tests.sh`) lulus pada salinan disposable `/tmp/cat-typecase-verify.3Of0dP`; log hasil ada di `all-tests.log`. Sanitizer tidak dijalankan terpisah karena tidak ada perubahan ownership/memori; test suite penuh tetap menjalankan pemeriksaan sanitizer yang tersedia.

Review independen read-only tidak menemukan masalah lain dalam batas case folding atau disambiguasi suffix; review mengidentifikasi batas dukungan Float128 yang dicatat di atas. Perubahan tetap unstaged, uncommitted, dan unpushed. Branch/worktree PR #15 tidak disentuh dan task terpisah berikutnya tidak dipilih atau dikerjakan.
