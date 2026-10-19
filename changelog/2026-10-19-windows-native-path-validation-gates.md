# Validasi driver Windows: pisahkan bukti path dari blocker PTY

[CI 39182327998](https://github.com/inggritsetyabudi/Cat/actions/runs/39182327998) menguji head `41ccb9a74e7907ac3b465982d3b60f16ba34164a` pada merge preview `b2e227d05251687efa47cb302dd7d3871276cf46`, dengan parent main terbaru `f3e9f54b7c5217d90622bea6093b35a2248bee70`. Pembaruan main hanya `ARCHITECTURE.md`; tree `claw/` sama dengan baseline `8a68609`.

Linux Release dan Linux ASan/UBSan/LSan lulus di CI dan pada clean snapshot lokal head tersebut. Review independen tidak menemukan blocker dalam scope driver/tes.

Windows berhasil melewati 169 pemeriksaan negatif komentar dan seluruh mode komentar valid, termasuk native build/eksekusi dengan path `valid comments` serta output persis. Kegagalan kemudian berpindah ke assertion PTY yang sudah ada di `run_lexer_comment_tests.py:134`: byte output dimulai dengan `ESC[6n` sebelum diagnostik lexer. Ini bukan bukti bahwa seluruh CI Windows lulus. Asal byte terminal tersebut belum diinvestigasi dalam workstream driver ini.

Gerbang path native baru dan round-trip argv kini dijalankan sebelum suite frontend/komentar agar kegagalan PTY tidak menghalangi bukti terarah untuk driver Windows. Semua tes lama tetap dijalankan sesudahnya dengan fail-fast yang sama; tidak ada spasi, assertion NO_COLOR, atau pemeriksaan lexer yang dihapus/dilonggarkan. Tidak ada perubahan pada implementasi atau runner lexer.

PR driver tetap draft dan tidak siap merge selama gerbang Windows gagal. Blocker PTY perlu ditangani sebagai workstream terpisah, dengan reproduksi dan perbaikan yang mempertahankan kontrak tes; PR #28 sudah merged sehingga follow-up bukan pembaruan head PR #28 yang masih terbuka. Hasil CI final dilaporkan di PR tanpa mengubah versi/tag/release atau status PRD.
