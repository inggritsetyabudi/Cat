# Jembatan migrasi native LLVM object — 2026-10-20

## Hasil utama

Pada branch `work/v0.1-v0.3-llvm-migration-20261020`, Cat sekarang dapat mengendalikan parsing/verifikasi LLVM IR, pemilihan target dan DataLayout, optimasi O0/O2, serta emisi ELF object melalui API LLVM di dalam proses. Ini irisan transisi yang opt-in, bukan migrasi native penuh atau bukti bahwa v0.1–v0.3 sudah matang.

Baseline sumber adalah remote `main` `74fab2e39e301a25b4a6ff97fa7d84f898e6fce1`. Implementasi produksi tidak disalin dari PR #36. Main, PR #36, PR #37, semantik bahasa, Arena self-reference, dan penolakan borrowed ordinary shape fields tidak diubah. Tidak ada merge, tag, versi, atau release yang dibuat. Label kanonis PRD/G2 tidak dinaikkan.

## Implementasi

- `claw/cmake/NativeObject.cmake` mengisolasi dependensi SDK. `CLAW_ENABLE_LLVM_OBJECT=OFF` secara default; build biasa tidak mencari SDK LLVM. Opt-in memerlukan `CLAW_LLVM_CONFIG` absolut, LLVM tepat **23.1.3**, header versi sesuai, komponen static yang benar-benar ada, dan host native Linux x86_64 GNU. LLVM 18 maupun konfigurasi tanpa SDK ditolak; tidak ada fallback PATH.
- `codegen/native_object.*` tidak mengekspos tipe LLVM pada API frontend Cat. Modul diparse dan diverifikasi; target Linux x86_64 ELF dan layout little-endian 64-bit diperiksa; layout LLVM TargetMachine diterapkan. Jalur release memakai pipeline O2 dan verifier kedua sebelum codegen; jalur debug memakai O0. Object memakai PIC dan diterbitkan dari staging file pada filesystem sama.
- Driver memiliki pilihan eksplisit `--native-backend=llvm-object` atau `--native-backend=clang`. Default tetap Clang. Compiler tanpa opsi SDK menolak pemilihan object dengan pesan jelas; tidak mencoba jalur legacy secara diam-diam.
- Pada jalur object, Clang eksternal menerima **object `.cat.o` dan runtime C**, bukan `.ll`; Clang masih bertanggung jawab atas kompilasi runtime dan link executable. Emitter LIR→LLVM text Cat yang sudah ada masih dipakai.
- IR sebelum optimasi tetap tersedia bersama DataLayout, dengan spelling konstanta Cat dipertahankan. Ini mencegah printer LLVM menulis ulang `i8 255` menjadi `i8 -1` dan merusak kontrak inspeksi regresi yang sudah ada.
- Object lama tidak ditimpa pada kegagalan parser/verifier/codegen/staging; staging gagal dibersihkan. IR/object yang tersedia tetap dapat dipakai untuk diagnosis jika linker gagal. Transaksi tiga artefak dan same-output concurrency **belum** ditutup.

Rincian penggunaan, pemisahan modul, dan urutan gerbang berikutnya ada di [panduan migrasi](../docs/native-llvm-migration.md).

## Pemeriksaan sebelum implementasi

Perilaku `check`, build native release, dan executable fixed Array Cat diuji sebelum pengubahan driver. Compiler baseline bersih juga dibangun dari `git archive` snapshot immutable; SHA-256 binary baseline adalah `ad3134585ddd07777790a6a52c4ea2e93981b9f5997cf2ffde4a569482565cae`. Baseline menghasilkan output shadowing/try `7`, `99`, `inner`, `0`, sesuai oracle suite native yang sudah ada; ekspektasi runner baru diperbaiki berdasarkan bukti itu, bukan dengan mengubah semantik atau melemahkan tes lama.

## Validasi yang sudah selesai

Lingkungan lokal Linux x86_64, compiler C++ Clang 18.1.3, SDK LLVM 23.1.3. Versi compiler C++ dan SDK LLVM adalah dua komponen yang berbeda; keberhasilan build/link bukan berarti LLVM 18 dipakai sebagai SDK object.

| Pemeriksaan | Hasil dan batas bukti |
| --- | --- |
| Build Release compiler default, SDK OFF | Lulus; jalur default tetap legacy Clang |
| Build Release compiler object, exact SDK 23.1.3 | Lulus, termasuk link closure static dan binary API test |
| `CLAW_EXE=<default>/claw bash claw/tests/run_all_tests.sh` | Lulus seluruh suite dari salinan artefak terisolasi |
| Suite native existing dengan wrapper pemilih LLVM object | Lulus; assertion existing tidak diubah. Termasuk owned-shape positives/negatives dan pemeriksaan artefak native |
| CTest API dan integrasi object | 2/2 lulus |
| Matriks object debug/release | Lulus output exact scalar, tuple, fixed Array, String/try; alias build-native, perbandingan baseline, default/explicit Clang, disabled/unknown backend, dan borrowed-field rejection |
| API negatives | Lulus malformed IR, verifier dominance, target kosong/salah, DataLayout salah, staging/publish failure; object sebelumnya dipertahankan |
| ELF dan argv linker | Lulus ELF64 little-endian relocatable x86_64; spy membuktikan satu invocation berisi object saat ini, runtime C, target eksplisit, tanpa `.ll`; path spasi/metakarakter aman |
| Kegagalan linker paksa | Lulus exit 91 dan diagnostik dipertahankan, tidak mengaku sukses, IR/object tetap tersedia, tidak menghasilkan executable baru |
| SDK negatif configure | LLVM 18.1.3 dan path SDK kosong ditolak sesuai diagnostik |
| Compiler ASan/UBSan + LSan, CTest object | 2/2 lulus dengan `detect_leaks=1:halt_on_error=1` dan UBSan halt-on-error |
| Full suite compiler ASan/UBSan + LSan | Lulus seluruh suite dengan compiler terinstrumentasi, leak detection aktif, artefak dan temporer terisolasi |
| Review independen terarah | Tidak menemukan bug baru yang bertentangan dengan batas experimental bridge; tidak menyatakan migrasi matang |

SDK LLVM static sendiri tidak dibangun ulang dengan sanitizer. Target sanitizer pada suite native existing merelink dump LLVM dengan Clang, sehingga bukan bukti bahwa **object hasil LLVM 23** sudah diinstrumentasi sanitizer. Windows/macOS/AArch64 dan CI exact-head belum menjadi bukti opt-in backend ini. Tidak ada klaim speedup benchmark.

## Temuan selama validasi

Percobaan pertama suite native object berhenti pada assertion spelling konstanta radix di dump `.ll`; output native benar, tetapi printer LLVM menormalisasi bit unsigned menjadi spelling signed. Implementasi diperkecil menjadi penyisipan DataLayout terverifikasi tanpa mencetak ulang IR Cat, ditambah regresi API `i8 255`. Suite native yang sama kemudian lulus tanpa assertion dilonggarkan.

Percobaan pertama full suite default terhenti pada `No space left on device`. Hasil tersebut tidak dilabeli lulus. Artefak baru dan tracked hasil uji disimpan sebelum baseline tracked dipulihkan, lalu seluruh suite diulang pada filesystem `/run` dengan artefak/temporer terisolasi dan lulus. Bukti, build cache, worktree lain, dan artefak PR #36 tidak dihapus.

Log lokal tersimpan di `/run/cat-migration-20261020/evidence/`, termasuk `environment.txt`, `default-full-suite-isolated.log`, `object-native-suite-stable-ir.log`, `object-ctest-stable-ir.log`, `object-integration-final-matrix.log`, `sanitizer-ctest.log`, dan catatan percobaan gagal. Direktori build default, object, sanitizer, dan baseline immutable juga dipertahankan.

## Gerbang yang tetap terbuka

Konstruksi LLVM langsung dari LIR Cat, pengambilalihan kompilasi runtime/link planning, transaksi executable dan concurrent same-output builds, feature/ABI/optimization matrix yang lebih luas, target-object sanitizer, bukti lintas platform pada SHA sama, serta seluruh acceptance gap v0.1–v0.3 tetap pekerjaan lanjutan. Windows stack `0xC0000005` versus `0xC00000FD` dari PR #36 masih belum terbukti sebabnya. Main tetap terlindungi sampai bukti kematangan lengkap dan keputusan merge memenuhi izin Setya.
