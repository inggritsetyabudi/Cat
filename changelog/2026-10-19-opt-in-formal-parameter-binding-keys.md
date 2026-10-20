# C@ — identitas formal parameter opt-in

Milestone ini melanjutkan PR #36 pada branch `poc/llvm-cpp-adapter-20261019`, dari `e0b08f6c36b25fe364cd9c40f4d3893e34cf20c0`. PR tetap draft dan `--local-binding-keys` tetap default OFF. Ini irisan metadata pada PoC LLVM C++ API + object native + LLD eksternal/runtime profile; bukan penggantian backend produksi, compiler mandiri penuh, atau penerimaan seluruh v0.1–v0.3.

## Correspondence yang ditambahkan

Deklarasi formal mempunyai tempat metadata sendiri pada AST. Saat opsi aktif, sema membuat token opaque pada deklarasi tersebut dan menyimpannya pada Symbol yang dipakai resolver. Token yang sama diteruskan ke resolved identifier, tracked parameter ownership serta cleanup/drop, formal parameter OIR/LIR, dan lookup slot adapter. Token tidak direkonstruksi dari nama, source line, parameter index, branch label, atau urutan lowering. Actual dan formal adalah deklarasi berbeda; pemanggilan tidak menyamakan token keduanya atau mengubah aturan transfer ownership/ABI.

Identitas hanya berlaku dalam hasil analisis yang bersangkutan. Tidak ada kontrak stabil lintas kompilasi, serialization, cache, clone/specialization, atau analyzer/modul berbeda. Semantik parameter yang immutable tetap immutable. Member identity, tuple/pick/try binders dan seluruh synthetic storage tidak termasuk perluasan ini. Receiver yang dianalisis melalui jalur parameter tidak memberi bukti dukungan ABI method/generic pada adapter.

## Batas yang tidak dilonggarkan

Sintaks, ownership/move/drop rules, borrow, Arena, borrowed fields, runtime, ABI dan driver produksi tidak diubah. Guard duplicate/unsupported adapter tetap fail-closed; hilangnya correspondence bukan alasan untuk mencari identity dari nama. Tanpa flag, jalur lama tetap berlaku termasuk empat penolakan sibling-owner pada corpus historis.

Implementasi berasal dari struktur semantik dan metadata C@ sendiri. Tidak ada penyalinan atau rename-only internals bahasa lain, ataupun nama pipeline baru yang dianggap sudah disetujui.

## Gerbang bukti

Corpus dan expected bytes/count lama dipertahankan. Test metadata lokal tetap memeriksa semua key lokal dan exact cleanup; expectation formal diperbarui hanya untuk membedakan default kosong dari opt-in terisi. Test parameter terpisah memeriksa correspondence deklarasi/use serta ownership/OIR/LIR nyata, shadowing, transfer dan cleanup. Corpus native parameter terpisah memakai expected output serta allocation count statis, produksi `check`/`oir`/`lir`/`llvm` dan build/run O0/O2, lalu PoC object → external LLD → native. Emission/link memakai PATH tanpa compiler dan paket runtime immutable.

Lock implementasi historis dan delta lokal pertama tidak ditulis ulang. Delta parameter tersendiri merekam hash sebelum yang harus cocok dengan rantai lama serta hash sesudah; semua file protected lainnya dan himpunan file tetap dikunci. Ini perubahan implementasi terukur, bukan klaim evidence-only byte-identical untuk path yang berubah.

Validasi final memerlukan clean Release, semua CTest dan suite produksi, perbandingan default 90 fixture × empat tahap, host/runtime sanitizer yang didukung, review independen pada exact SHA dan CI Linux + Windows UCRT. Hasil aktual, SHA final, ledger, review dan CI dicatat di komentar PR setelah run, bukan dianggap PASS karena gate sudah ditulis. Windows sanitizer yang tidak tersedia tetap **SKIP**, bukan PASS. Instrumentasi C runtime tidak membuktikan seluruh instruksi LLVM yang dihasilkan terinstrumentasi.

## Yang masih terbuka

Belum ada promosi default, merge, perubahan status rilis, atau klaim canonical universal. Formal metadata pada subset yang terbukti tidak menyelesaikan semua pattern binders, synthetic/projection provenance, generics/method ABI, workspace/import atau seluruh interaksi sintaks v0.1–v0.3. Kegagalan ANSI Windows pada run historis pernah lolos rerun pada SHA sama tanpa perubahan; penyebabnya belum diketahui dan tidak boleh dianggap diperbaiki oleh patch ini.
