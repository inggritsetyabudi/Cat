# 2026-10-20 — Parent directory isolasi harness singleton Windows

Exact-head CI `e7ad9c9c311c706fb427fad0d02f5a3392da4439` berakhir 9/10 check hijau. Gate Windows UCRT gagal di setup test `llvm-cpp-singleton-native`, sebelum emisi/link/run: pemanggil belum membuat parent `emitter-isolation`, sedangkan helper historis `isolated_windows_emitter` hanya membuat child `windows-emitter-isolation` (`WinError 3`). Production roundtrip, flag-OFF rejection, dan seluruh 66 kasus admission pada Windows sudah lulus dalam run gagal yang sama. Failure asli dipertahankan di [run 39376074381](https://github.com/inggritsetyabudi/Cat/actions/runs/39376074381).

Perbaikan hanya pada harness baru: membuat parent milik test sebelum memanggil helper tanpa mengubah helper/oracle historis; menggunakan path salinan emitter yang dikembalikan helper, bukan sekadar environment-nya. Dengan demikian emisi Windows benar-benar dijalankan dari direktori kosong kecuali emitter dan closure DLL non-system yang diaudit. Tidak ada compiler baru pada PATH isolasi, dan tidak ada perubahan validator, lowering, produksi, ABI, source-language semantics atau runtime/archive.

Sebelum koreksi ini, local Linux Release dan host ASan/UBSan masing-masing telah menyelesaikan 23/23 CTest dan seluruh suite produksi dalam disposable source copies; compiler subtree identik pada head koreksi. Hasil tersebut bukan bukti eksekusi native Windows. Kelulusan Windows baru boleh dinyatakan setelah run exact-head koreksi selesai.

PR #36 tetap draft, opsi default OFF, tanpa merge/promotion. Windows sanitizer tetap SKIP bila tidak tersedia. Terminal access violation stack attempt1 tetap failure nyata yang belum terselesaikan dan tidak diubah oleh perbaikan test harness ini.
