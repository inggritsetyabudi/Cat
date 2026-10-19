# Paired native String/Str ABI–cleanup oracle (2026-10-19)

Milestone ini hanya menambahkan regression evidence untuk jalur String/Str **produksi**. Tidak ada dukungan String baru pada adapter LLVM C++, perubahan lowering/runtime/ABI produksi, lifetime syntax, borrowed-field exception, Arena, f-string/Show, PRD/version/release marker, atau perubahan default OFF.

## Tambahan test-only

- `claw/experimental/llvm_cpp/abi_oracle/` berisi fixture ABI caller/callee, runner lintas target, layout oracle yang meng-include runtime produksi, allocation/free observer dan kontrol negatif observer.
- Gate `llvm-cpp-production-string-abi-oracle` menuntut signature/call operands dan klasifikasi LIR per-target, compiled C layout, raw native output/exit, exact allocation/free balance serta zero outstanding/untracked frees.
- Corpus mencakup Str helper dan borrowed helper, owned String argument/return result slot, constructor-return, `String.from`/`.as_str`/clone, scope/temporary/branch/pick/scan/loop/early-return/try-failure, empty/multibyte UTF-8/embedded NUL sesuai subset produksi.
- Adapter wajib menolak input yang sama pada default dan seluruh flag I32 tanpa requested `.o/.ll`. Scalar control tetap harus menerbitkan object. Tidak ada fallback ke driver produksi untuk penolakan adapter.

## Target dan instrumentasi

Workflow menjalankan focused oracle pada native Linux x86_64 ELF Release/sanitizer-host dan native Windows x64 MinGW UCRT pada exact PR head. Source LLVM IR, target triple/DataLayout, record layouts, seluruh declarations/definitions/calls, runtime/linkage symbols, profiles, perintah, stdout/stderr bytes dan ledger allocation disimpan sebagai evidence.

Observer hanya membungkus malloc/free runtime TU dan tetap menjalankan allocator sebenarnya. Kontrol leak/untracked membuktikan runner tidak memperlakukan exit0 sebagai ledger PASS. Observer bukan ASan, bukan profiler heap universal, dan tidak membuktikan seluruh ownership/liveness bahasa.

Sanitizer compile/run probe mencatat opsi yang benar-benar tersedia. Linux sanitizer target mandatory; unavailable Windows options dilaporkan SKIP dengan alasan aktual dan native observer sebagai bukti allocation/free yang terpisah. Jangan mengubah SKIP menjadi sanitizer PASS.

11 gate lama, legacy 41 cases, runtime/arithmetic cases dan oracle produksi dipertahankan. Full production suite artifact-mutating dijalankan pada checkout disposable terpisah dari checkout evidence. LLVM/LLD tetap exact23.1.3. Hasil kelulusan native, sanitizer, independent review dan CI harus dibaca pada **exact SHA yang benar-benar dijalankan**, bukan diasumsikan dari hadirnya harness ini. Kelulusan oracle tidak mengaktifkan String adapter atau mempromosikan backend ke produksi.

## Koreksi plumbing native Windows

Candidate `f7051b427b4ca823727eddc21712b4a9eee713ba` memperoleh **7/8** job CI; Windows PoC gagal sebelum menguji ABI karena native Python UCRT tidak menemukan `git` di PATH MSYS minimal. Ini kegagalan gate nyata, bukan bukti String ABI yang gagal atau alasan untuk menandai Windows PASS. CMake kini meresolusi executable Git (termasuk instalasi native Git for Windows), runner menerima path eksplisit, dan full-suite worktree Windows memakai executable yang sama. CTest juga memasok expected SHA hasil konfigurasi sehingga standalone gate tidak sekadar mengandalkan environment optional. Tidak ada perubahan expected source/output/count/signature atau runtime/backend. Exact SHA pengganti wajib diuji ulang di semua job dan review.
