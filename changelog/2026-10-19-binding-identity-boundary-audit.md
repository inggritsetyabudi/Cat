# 2026-10-19 — audit batas binding identity String, tanpa implementasi

## Keputusan

**STOP pada audit/evidence.** Implementasi sumber produksi, ownership, OIR/LIR, backend tekstual, runtime dan adapter LLVM C++ tetap byte-identical dengan `e394800450593026426c3f56a18ac4473864d997`. Tidak ada ID yang ditebak dari nama, source line, urutan block atau urutan instruction. Sibling owner tetap ditolak sebelum publikasi object/IR. PR36 tetap draft dan seluruh opsi PoC tetap default OFF.

## Titik identity yang belum lengkap

`SemanticAnalyzer::defineVariable` (`claw/src/analysis/sema.cpp:4987–4993`) hanya memberikan `$local.binding.N` ketika nama yang sama sudah terlihat dari scope luar. Dua ordinary declaration pada sibling branch tanpa binding luar dapat sama-sama mempunyai `backendBindingKey` kosong. `IdentExpr` (`:3956–3969`) menyalin key dari Symbol, bukan declaration ID universal.

`OwnershipChecker::defineTrackedVar` (`claw/src/analysis/ownership.cpp:1399–1415`) mempunyai `ownership-binding-N` internal untuk borrow/lifetime ordering, tetapi `DropAction` di scope exit (`:1369–1396`) hanya membawa `backendBindingKey`. ID internal ownership bukan ID declaration/use/drop yang dapat dipulihkan adapter. Perbaikan `OirValue.bindingKey` existing meneruskan key yang tersedia, bukan menciptakan identitas yang hilang.

OIR/LIR meneruskan key ordinary object/store/value/drop bila tersedia. Formal parameter belum mempunyai field key (`oir.h:40–44`, `lir.h:35–39`); synthetic result/temporary dan beberapa tail stores menggunakan namespace hasil internal, bukan universal declaration ID. Ini menutup jalan untuk menganggap perubahan satu guard adapter sebagai koreksi lokal yang sudah terbukti aman. Tidak adanya field literal bernama `bindingId` bukan persoalan; yang hilang adalah correspondence opaque declaration/use/drop yang universal.

Tidak diklaim ada collision SSA produksi pada repro yang berhasil. Backend tekstual sudah mempunyai deduplikasi nama alloca; keberhasilan native dan nama LLVM unik tidak membuktikan adanya identitas semantik unik sebelum LIR.

## Gate evidence-only tambahan

`llvm-cpp-binding-identity-audit` menambah enam source nyata: sibling output, sibling early return, nested sibling branches, nested owner shadow, parameter shadow, dan Bool-loop sibling branches. Setiap source memakai driver produksi `check`, `oir`, `lir`, `llvm`, build default-O0 dan `--release`/O2, kemudian native run dengan expected bytes statis. Empat sibling-ambiguity inputs wajib ditolak PoC `--strings` tanpa object/IR; dua shadow controls tetap harus emit/link/run. Seluruh 13 gate lama dan corpus 41 scalar, 29 arithmetic, 19 String dan 14 ABI oracle tidak diubah.

Observer runtime prebuilt yang sama dengan gate String digunakan untuk 22 allocation/free per profile, 44 per dua profile per target, dengan zero outstanding/untracked frees. Source reference IR dikompilasi hanya pada jalur pembanding produksi. Link observer/sanitizer dan link PoC memakai LLD dengan PATH compiler-free, tanpa `.cat`/IR/C pada argv final link. Input runtime/archive dilindungi stamp/hash. Linux instrumentasi C-runtime ASan/UBSan/LSan menggunakan paket/probe existing; availability Windows ditulis apa adanya, bukan SKIP yang disebut PASS. Tidak ada klaim semua instruksi LLVM terinstrumentasi atau pembuktian bebas leak di luar corpus.

Lock 68 file implementasi/oracle baseline menegakkan sifat evidence-only, termasuk pada checkout CI shallow. Expected output/count tidak diambil dari hasil compiler. Gate memerlukan preparation milik String gate; jalankan seluruh CTest atau kedua gate bersama, bukan audit saja pada build kosong:

```sh
cmake --build <build> --parallel 2
ctest --test-dir <build> -R '^llvm-cpp-(poc-strings|binding-identity-audit)$' --output-on-failure
```

## Bukti yang diperlukan sebelum implementasi berikutnya

Rancang canonical opaque ID pada semantic declaration creation dan simpan correspondence yang sama untuk Symbol, resolved use, assignment target, owner/borrow state, cleanup/drop, params, pattern binders, synthetic owned temporaries, OIR/LIR dan LLVM slots. Bedakan source bindings dari synthetic SSA/storage dan tetapkan lifetime serta aturan specialization/clone secara eksplisit. Allocator token internal sah bila dialokasikan sekali dan diteruskan; merekonstruksi ID dari urutan lowering tidak sah. Field access harus mempertahankan identity object base dan resolved field, bukan membuat owner baru dari nama field.

Baru setelah invariant itu tersedia sebelum LIR dan teruji pada scope/loop/move/early exit, tinjau ulang implementasi di workstream PR36 yang sama. Tidak ada perubahan sintaks, aturan move/drop, ABI runtime, Arena, PRD, versi, release atau production switch dalam milestone ini. Hasil native/CI dan fresh review harus dilaporkan pada exact SHA evidence final; konfigurasi gate bukan bukti kelulusan.
