# Changelog C@

Folder ini menyimpan log perubahan compiler dan dokumentasi, termasuk rincian historis yang sebelumnya berada di `changes/`.

- [`2026-10-05.BugFixes-Portability-TestGreen.md`](./2026-10-05.BugFixes-Portability-TestGreen.md) dipindahkan dari `changes/` saat folder lama dihapus. Rincian audit dan verifikasi historisnya dipertahankan.
- Berkas-berkas berikutnya mencatat perubahan per topik/tanggal. Changelog mendokumentasikan implementasi dan validasi yang terjadi; dokumen desain kanonis tetap berada di [`cat-northstar/`](../cat-northstar/).
- [`2026-10-11-v0.2-try-owned-result-transfer.md`](./2026-10-11-v0.2-try-owned-result-transfer.md) mencatat konsumsi ownership `Result` dan native ASan/LSan.
- [`2026-10-11-v0.2-try-import-alias-type-identity.md`](./2026-10-11-v0.2-try-import-alias-type-identity.md) mencatat identitas nominal tipe generic lewat import alias.
- [`2026-10-11-v0.2-operator-groups-a-b.md`](./2026-10-11-v0.2-operator-groups-a-b.md) mencatat irisan operator A–B, riwayat baseline, dan batas saat ini setelah follow-up indexing Span.
- [`2026-10-11-v0.2-index-compound-span.md`](./2026-10-11-v0.2-index-compound-span.md) mencatat dukungan terbatas `values[next_index()] += 1` pada mutable `Span[Int32]`; general `Array`/`Vec` indexing dan grup B tetap parsial.
- [`2026-10-11-v0.2-implicit-return.md`](./2026-10-11-v0.2-implicit-return.md) mencatat return implisit ekor blok, warning nonfatal, serta status validasi slice spesifik tanpa mengubah status §3/v0.2.
- [`2026-10-11-v0.2-discarded-owned-temporary-drop.md`](./2026-10-11-v0.2-discarded-owned-temporary-drop.md) mencatat temuan leak `Anchor` dari review independen dan koreksi destructor beserta validasi ASan/LSan.
- [`2026-10-11-v0.2-owned-rvalue-discard-cleanup.md`](./2026-10-11-v0.2-owned-rvalue-discard-cleanup.md) mencatat fix leak owned rvalue lintas AST, audit value-category, regression `BinaryExpr`/`Add`, dan hasil review terarah.
- [`2026-10-11-v0.3-plain-tuples-partial.md`](./2026-10-11-v0.3-plain-tuples-partial.md) mencatat implementasi parsial tuple plain/proyeksi, contextual return typing, destructuring datar, serta batas parser newline.
- [`2026-10-11-v0.3-tuple-destructuring-partial.md`](./2026-10-11-v0.3-tuple-destructuring-partial.md) merinci pola tuple datar yang didukung, aturan konsumsi source, tes, dan batas fitur yang tersisa.
- [`2026-10-11-v0.3-tuple-pick-patterns-partial.md`](./2026-10-11-v0.3-tuple-pick-patterns-partial.md) mencatat pola tuple rekursif di `pick`, ownership selector, regresi frontend/AIR/OIR/native, dan batas field yang tetap ditolak.
- [`2026-10-11-v0.3-owned-tuple-cleanup-partial.md`](./2026-10-11-v0.3-owned-tuple-cleanup-partial.md) mencatat cleanup aman untuk tuple ber-field `Anchor` plain, regresi sanitizer, dan batas ownership yang tetap ditolak.
- [`2026-10-12-anchor-nested-payload-safety.md`](./2026-10-12-anchor-nested-payload-safety.md) mencatat baseline leak 4 byte, penolakan nested Anchor payload di sema, dan defer recursive drop glue.
- [`2026-10-12-air-tuple-expression-rendering.md`](./2026-10-12-air-tuple-expression-rendering.md) mencatat rendering rekursif TupleExpr di AIR, golden tuple, dan validasi output non-tuple.
- [`2026-10-12-int32-ranges-partial.md`](./2026-10-12-int32-ranges-partial.md) mencatat dukungan parsial `Range[Int32]`/`in`, ABI/native regressions, serta defer iterasi, slicing, `Char`, dan integer widths lain.
- [`2026-10-12-int32-range-scan.md`](./2026-10-12-int32-range-scan.md) mencatat iterasi `Range[Int32]` tanpa indeks/dengan indeks `USize`, batas inclusive tanpa overflow, dan batas unsupported iterable.
- [`2026-10-12-char-ranges-partial.md`](./2026-10-12-char-ranges-partial.md) mencatat konstruksi/membership/scan `Range[Char]`, progres scalar yang melewati surrogate, dan batas slicing yang tetap `Range[Int32]`.
- [`2026-10-12-range-scan-shadowed-binding-identity.md`](./2026-10-12-range-scan-shadowed-binding-identity.md) mencatat perbaikan identity binding untuk nested scan yang men-shadow binding luar.
- [`2026-10-12-collection-truncate-fail-closed.md`](./2026-10-12-collection-truncate-fail-closed.md) mencatat penolakan semantik `Vec.truncate`/`Queue.truncate` karena kontrak drop dan lowering native belum lengkap.
- [`2026-10-12-ordinary-local-shadowing-binding-identity.md`](./2026-10-12-ordinary-local-shadowing-binding-identity.md) mencatat keputusan shadowing leksikal, key binding biasa di backend, regresi interaksi local/scan, dan validasi full suite.
- [`2026-10-12-builtin-member-receiver-binding-identity.md`](./2026-10-12-builtin-member-receiver-binding-identity.md) mencatat penerusan key receiver builtin member hingga LLVM dan regresi native Anchor shadowing.
- [`2026-10-12-try-pick-binder-binding-identity.md`](./2026-10-12-try-pick-binder-binding-identity.md) mencatat identitas binder hasil `try`, payload choice/tuple `pick`, cleanup ownership, serta sanitizer regressions.
- [`2026-10-12-str-bounds-check-binding-identity.md`](./2026-10-12-str-bounds-check-binding-identity.md) mencatat penerusan identitas receiver ke bounds-check LIR/LLVM, regresi `Str` shadowing, dan probe cleanup `try`/`pick`.
- [`2026-10-12-char-literals-v04.md`](./2026-10-12-char-literals-v04.md) mencatat dukungan literal Char Unicode scalar dari lexer sampai native LLVM `i32`, validasi UTF-8/escape, dan regresi sanitizer.
- [`2026-10-12-char-integer-surrogate-followup.md`](./2026-10-12-char-integer-surrogate-followup.md) mencatat penolakan integer surrogate/di atas U+10FFFF untuk target `Char`, regresi signed literal, dan verifikasi review final.
- [`2026-10-12-str-range-views-partial.md`](./2026-10-12-str-range-views-partial.md) mencatat range-view `Str` berbasis byte, validasi batas UTF-8, `get(range) -> Maybe[ref Str]`, dan status slice yang tetap parsial.
- [`2026-10-13-cli-build-status.md`](./2026-10-13-cli-build-status.md) mencatat progress bar berwarna C@ Forge, status sukses/gagal, pelestarian diagnostik/exit code, dan batas profile native `-O2` saat ini.
- [`2026-10-13-cli-build-status-truthful-progress.md`](./2026-10-13-cli-build-status-truthful-progress.md) mencatat penghapusan estimasi persen yang tidak terukur, spinner indeterminate untuk lowering/link, dan regresi CLI otomatis.
- [`2026-10-13-cli-build-status-review-fixes.md`](./2026-10-13-cli-build-status-review-fixes.md) mencatat koreksi status proses clang, cleanup diagnostik RAII, link Threads, dukungan TERM, dan validasi ulang full suite.
- [`2026-10-13-cli-build-status-direct-linker-launch.md`](./2026-10-13-cli-build-status-direct-linker-launch.md) mencatat peluncuran clang POSIX tanpa shell, klasifikasi spawn/exit/signal yang akurat, dan regresi argumen path.
- [`2026-10-13-cli-build-status-tempfile-hardening.md`](./2026-10-13-cli-build-status-tempfile-hardening.md) mencatat pemakaian descriptor `mkstemp` langsung untuk stderr clang, cleanup spawn actions, dan hasil validasi akhir.
- [`2026-10-13-cli-build-status-diagnostics-fd-read.md`](./2026-10-13-cli-build-status-diagnostics-fd-read.md) mencatat replay diagnostik clang berbatas melalui descriptor terbuka, regresi pathname pengganti, dan validasi CLI terfokus.
