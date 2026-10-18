# Bukti dokumentasi G3–G8, tanpa menutup G2

Baseline pekerjaan ini adalah PR #27 head `67eb49f34f2cdc634ed90af308238d208eca76c3`, branch `docs/g2-feature-status-20261018`. Acuan normatif tetap [PRD-CHANGES §2](../cat-northstar/PRD-CHANGES-v0.1-to-v1.0.md#2-perubahan-global-berlaku-di-seluruh-prd) dan checklist migrasi §3. Tidak ada perubahan semantik compiler, source fixture, arsip, atau PR #22–#26. Label compiler tidak dipromosikan. **G2 NOT CLOSED.**

## G3: constructor dan artefak bernama

Review read-only baseline tidak menemukan `Tipe(field: ...)` dalam fence contoh aktif. `Config { ... }` dalam `implements Config` pada PRD §1 adalah literal modul pendefinisi yang sah, bukan temuan. Penyebutan bentuk lama pada tabel risiko PRD L107 dan instruksi Arena L617 merupakan prosa migrasi, bukan contoh aktif yang perlu diganti membabi buta.

Baseline belum menyediakan artefak pengganti untuk semua nama yang diminta. [GLOBAL-EXAMPLES.md](../cat-northstar/GLOBAL-EXAMPLES.md) kini menyediakan cuplikan migrasi **parsial**:

- Config shape/choice: PRD §1 sudah memperlihatkan constructor Result dan literal lokal; contoh identitas baru memakai `try Config.new` sebelum mengambil `ref`, bukan memperlakukan Result sebagai Config.
- Buffer/Circle: `new` berisi literal lokal; pemanggil memakai `.new(...)`. `Buffer` memakai `vec[]`, bukan constructor koleksi lama.
- Vector2: hasil `add` dan pemanggilan contoh memakai `.new(...)`; implementasi kontrak lengkap tidak diklaim.
- Node/Cache: panggilan `.new(...)` ditampilkan dengan asumsi constructor disediakan. Contoh Arena tetap proposal/eksperimental; tidak membuktikan lifetime, borrowed storage, atau safety. Cache memakai owned String sesuai keputusan aktif, bukan anggapan lama bahwa Str owned.

Arsip tetap jejak asal, bukan contoh aktif. Cuplikan pengganti tidak membuktikan seluruh bab historis, konteks negatif, constructor locality lintas modul, atau migrasi graph/doubly linked ke `vec` + indeks. Residual setiap artefak tetap di ledger.

## G4: casing dan enam bagian historis

Audit membedakan nama tipe dan parameter tipe, builtin scalar/alias case-insensitive dari daftar tertutup PRD §4, dan identifier nilai. Pengecualian varian kapital hanya **Ok, Fail, Some, None**, bukan semua nama yang mirip. Varian user `circle`, `rect`, `none` pada fence PRD §4 sudah lowercase. Fixture negatif casing bukan contoh yang mempromosikan sintaks lama.

Checklist keenam bagian yang disebut PRD-CHANGES S84, dengan nomor historis (bukan menyamakan nomor heading aktif):

| Bagian historis | Tujuan aktif yang ditinjau | Hasil dan batas |
| --- | --- | --- |
| §1 Fungsi/blok | PRD §1/§3/§10; README; SPEC | Fence fungsi, binding, field dan pemanggilan ditinjau; tidak ada nama nilai kapital stale yang ditemukan. Nama tipe/scalar dipertahankan. |
| §2 Borrow/view | PRD §9/§14; SPEC | Nama contoh lowercase; `Str`, `Span`, generic/scalar adalah tipe. Tidak membuktikan semua aturan borrow. |
| §17 Anonymous function | PRD §20 butir 5 | Hanya aturan closure dalam prosa; belum ada bagian/contoh closure lengkap S82. Tidak dihitung sebagai contoh lulus. |
| §29 Borrow formal | PRD §9/§14; SPEC | Fence aktif diperiksa, bukan token implementasi C++/pesan source. Bukti parser/borrow tetap residual terpisah. |
| §30 Toolchain | docs/BUILDING.md, docs/CLI.md, docs/TESTING.md, claw/README.md | Contoh C@ aktif dan perintah shell dibedakan. Huruf opsi CLI/environment bukan identifier C@. |
| §32 Anonymous inference | PRD §20 butir 5, skeleton generic PRD §13 | Skeleton generic bertipe kapital sah; contoh inferensi closure penuh belum ada, sehingga tidak diberi tanda selesai. |

Ini merupakan hasil audit contoh yang tersedia, bukan completion seluruh kewajiban S84. Scanner leksikal menjadi pencegah regresi bounded, bukan parser bahasa atau pembuktian seluruh nama/resolusi tipe.

## G5 dan G8: hanya contoh kode aktif

Tidak ditemukan stale identifier `Option` pada contoh aktif baseline; tidak ada contoh yang perlu diganti menjadi Maybe. Kata Option dalam aturan penghapusan/prosa sejarah sengaja dipertahankan. `Maybe`, `Some`, `None` adalah ejaan yang digunakan contoh carrier aktual.

Tidak ditemukan `modules.cat`, `pub`, atau `realm` dalam contoh aktif baseline. Larangan di PRD §17/README/SPEC bukan pelanggaran. Source lexer, runner yang menyusun tes penolakan, dan fixture negatif bukan contoh bahasa aktif. Validator memisahkan fence program dari prosa, komentar, string biasa, teks diagnostik, dan riwayat. Import path tetap diperiksa sebagai kode, bukan dibuang sebagai prosa.

Inventaris eksplisit mencakup **18 dokumen aktif** di root, `docs/`, `claw/docs/`, serta dokumen desain/contoh aktif `cat-northstar/`. Scanner memeriksa **9 fence cat dan 20 fence cat proposal**; satu fence expected-error tetap menerima pemeriksaan token G5/G8, sedangkan deklarasi casing/error disengaja tidak dipromosikan sebagai contoh positif. **41 fence non-C@** (shell, teks, dan lainnya) dikecualikan; tidak ada fence tanpa label yang tersisa pada inventaris ini. Tiga contoh roadmap yang sebelumnya tanpa label kini berlabel `cat proposal`, tanpa perubahan isi atau status keputusan. Proposal **tetap dipindai secara leksikal**, meskipun tidak dikompilasi oleh doc-test. Regresi CLI membuktikan stale proposal ditolak dan prosa migrasi yang sama tidak ditandai.

## G6: dua contoh, dua pemeriksaan

[PRD: contoh entrypoint](../cat-northstar/PRD.md#contoh-entrypoint-kanonis-dan-varian-sah-g6) kini menunjukkan satu fence `cat` berisi `fn main() -> Int32 { return 0 }`, serta fence `cat` terpisah `fn main() {}` yang secara eksplisit diberi nama **Varian sah — Unit**. [Runner](../claw/tests/docs/run_doc_tests.py) membuat source temporary dan menjalankan `claw check` untuk setiap fence, sehingga dua signature tidak disimpulkan dari satu fixture. Regresi memastikan keduanya benar-benar fence executable berbeda.

Jumlah PRD sekarang **19 fence**: **3 cat positif**, **1 cat error**, **13 proposal**, **2 text**, **0 tanpa label**. Ini bukti `check`, bukan matriks exit code native atau seluruh aturan entrypoint. Nomor baris sumber lama tidak digeser: contoh baru ditambahkan di akhir PRD; jumlah fence pada rencana diperbarui tanpa menambah baris sumber.

## G7: tiga kewajiban dan dependensi terbuka

G7 kini dipisah menjadi tiga owner dokumentasi agar satu keberhasilan tidak menggantikan yang lain:

1. **G2-PLAN-G7 — encoding UTF-8:** dua dokumen arsip `legacy/cat-language-design-summary*.md` dapat didekode UTF-8 secara strict dan round-trip byte. Ini hanya bukti encoding arsip, bukan output ringkasan aktif.
2. **G2-PLAN-G7-RINGKAS-SOURCE — sumber:** baseline tidak mempunyai baris data `Ringkas:` per bagian pada PRD aktif; instruksi yang menyebut kata itu bukan data sumber generator.
3. **G2-PLAN-G7-GENERATOR-PROVENANCE — output:** tidak ada generator atau output ringkasan aktif pada branch ini. Determinisme, reproduksi output dari sumber, dan UTF-8 output **tidak dapat diuji**; hasilnya missing, bukan pass.

[PR #26](https://github.com/inggritsetyabudi/Cat/pull/26) dikonfirmasi **OPEN**, `mergedAt: null`, head `bd035818ca9bdbf55366bd1a9502739483c2ed10` ketika audit. Ia dependensi terbuka, tidak diubah, tidak di-cherry-pick, dan tidak dikreditkan sebagai baseline PR #27. Menambah pipeline Ringkas di sini akan mencampurkan lingkup PR lain, sehingga tidak dilakukan.

## Integritas dan verifikasi

Ledger, manifest evidence, dan source index mempunyai **365 ID**: **54 diterima compiler**, **295 diputuskan**, **16 diusulkan**. Kenaikan dua ID hanya memisahkan kewajiban G7, bukan fitur bahasa baru. Source excerpt kanonis dipertahankan; assertion dokumentasi tidak dipakai untuk promosi compiler.

Perintah pemeriksaan: validator ledger/source index; scanner contoh aktif; `python3 -m unittest discover -s claw/tests/docs -p 'test_feature_status*.py' -v`; `bash claw/tests/docs/run_doc_tests.sh`; suite penuh `bash claw/tests/run_all_tests.sh`. Hasil run, independent review, dan CI Linux/Windows pada head final dilaporkan di PR, bukan diasumsikan dari baseline. Semua residual di atas tetap berlaku walaupun pemeriksaan mekanis lulus.

### Hasil lokal dan review independen

Validator ledger/source-index dan scanner contoh lulus. **76 regresi** lulus, termasuk **11 regresi scanner** (prosa false-positive, stale proposal true-positive, constructor multiline, casing/alias, Option sebagai G5, modules.cat termasuk namespace bertingkat). PRD doc-test **COMPLETE**: 19 fence, empat executable (tiga lolos dan satu ditolak sesuai diagnostik), 13 proposal serta dua text dilaporkan terlewati. Suite penuh existing (dokumentasi/perintah CLI, frontend, OIR, LLVM backend, native, CLI status) lulus di salinan source sementara terpisah; binary compiler baseline dipakai ulang karena source compiler tidak berubah.

Review independen dokumentasi/governance dan review independen scanner/regresi sama-sama tidak menemukan blocker, dengan batas bukti proposal/leksikal serta G7 yang tetap missing. Reviewer scanner menjalankan ulang 76 regresi dan doc-test dengan binary yang disediakan. Hasil CI Linux/Windows pada head yang didorong tetap dicatat pada PR setelah selesai; hasil lokal ini bukan pengganti CI atau penutupan G2.
