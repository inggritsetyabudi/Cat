#!/usr/bin/env python3
"""Independent, source-bound inventory requirements for non-rewrite roadmap atoms.

The source is never generated or modified. Each tuple specifies one bounded
proposal, its original source line, and a verbatim distinguishing phrase.
Ratified names/staging/basic rules are cross-referenced, not reclassified.
"""
from pathlib import Path

# section, suffix, original source line, exact phrase, bounded semantic scope
SPECS = [
    (0, 'DESIGN-FIRST', 11, 'Desain tertulis lebih dulu', 'Desain fitur pasca-v1 ditulis lebih dulu.'),
    (0, 'NON-GOALS', 11, 'daftar apa yang sengaja tidak dikerjakan', 'Desain menyertakan daftar yang sengaja tidak dikerjakan.'),
    (0, 'NEGATIVE-TESTS', 12, 'program yang harus ditolak', 'Fitur pasca-v1 menyertakan tes negatif khusus Safe.'),
    (0, 'MEASURABLE-CRITERIA', 13, 'yang terukur untuk fitur itu', 'Fitur pasca-v1 mempunyai kriteria Fast/Safe/Simple terukur.'),
    (0, 'BOOTSTRAP-GATE', 14, 'tetap menghasilkan biner identik', 'Fitur pasca-v1 lolos bootstrap dengan hasil biner identik.'),
    (1, 'V11', 23, 'v1.1', 'Urutan v1.1 untuk pustaka standar/tooling adalah usulan prasyarat self-hosting dan pengujian Simple.'),
    (1, 'V12', 24, 'v1.2', 'Penempatan bootstrap pada v1.2 adalah usulan; mulai setelah v1 tetap keputusan staging, bukan target versi yang sudah diratifikasi.'),
    (1, 'V13', 25, 'v1.3', 'Penempatan rewrite pada v1.3 dan ekspander ditulis dalam C@ adalah usulan; staging ratifikasi v1.x tetap G2-PRD-15-REWRITE.'),
    (1, 'V14', 26, 'v1.4', 'Urutan threading pada v1.4 sebagai fondasi concurrency aman adalah usulan.'),
    (1, 'V15', 27, 'v1.5', 'Urutan channel pada v1.5 setelah threading adalah usulan.'),
    (1, 'V16', 28, 'v1.6', 'Urutan profil embedded pada v1.6 adalah usulan.'),
    (1, 'V17', 29, 'v1.7', 'Urutan keputusan async pada v1.7 adalah usulan dan hanya dikerjakan bila terbukti perlu.'),
    (1, 'V20', 30, 'v2.0', 'Penempatan penguatan formal, ref mut[s], SIMD dan backend tambahan pada v2.0 adalah usulan.'),
    (2, 'LIBRARY-FILE-IO', 36, 'file I/O', 'Cakupan pustaka v1.1 mencakup file I/O; kecukupan workload v1 dimiliki G2-PLAN-M10-SELFHOST-FILE-IO.'),
    (2, 'LIBRARY-STRING-BUILDER', 36, 'string builder', 'Cakupan pustaka v1.1 mencakup string builder; API dasar dan kecukupan v1 tetap pada owner PRD/PLAN.'),
    (2, 'LIBRARY-VEC', 36, '`vec`', 'Cakupan pustaka v1.1 mencakup vec; literal dan kecukupan v1 tetap pada owner PRD/PLAN.'),
    (2, 'LIBRARY-MAP', 36, '`map`', 'Cakupan pustaka v1.1 mencakup map; literal dan kecukupan v1 tetap pada owner PRD/PLAN.'),
    (2, 'LIBRARY-SET', 36, '`set`', 'Cakupan pustaka v1.1 mencakup set; ini bukan duplikasi keputusan literal set.'),
    (2, 'LIBRARY-TIME', 36, 'waktu', 'Cakupan pustaka v1.1 mencakup waktu.'),
    (2, 'LIBRARY-PROCESS-ARGS', 36, 'argumen proses', 'Cakupan pustaka v1.1 mencakup argumen proses.'),
    (2, 'LIBRARY-MAYBE-RESULT-HELPERS', 36, '`Maybe`/`Result` helper', 'Cakupan pustaka v1.1 mencakup helper Maybe/Result, bukan penamaan atau varian dasar tipe.'),
    (2, 'FMT', 37, 'satu gaya, tanpa opsi', 'claw fmt memakai satu gaya tanpa opsi.'),
    (2, 'DOC', 37, 'dari `///` pada item `share`', 'claw doc menghasilkan dokumentasi dari /// pada item share; visibilitas dasar tetap G2-PRD-17-SHARE.'),
    (2, 'TEST', 37, 'dari `@test`', 'claw test menggunakan @test; keberadaan annotation bawaan dimiliki G2-PRD-15-ANNOTATION-TEST.'),
    (2, 'LANGUAGE-SERVER-INLAY', 37, 'inlay hint nama parameter', 'Language server memberikan inlay hint nama parameter sebagai mitigasi tanpa argumen bernama.'),
    (2, 'SELFHOST-ADEQUACY', 38, 'hanya dengan pustaka ini', 'Compiler C@ dapat ditulis hanya memakai pustaka yang diusulkan di bagian ini; berbeda dari kecukupan empat komponen gerbang v1.'),
    (3, 'STAGE0', 48, 'compiler C++ saat ini', 'stage0 adalah biner awal yang dibangun oleh compiler C++ saat ini.'),
    (3, 'STAGE1', 49, 'stage0 mengompilasi sumber C@', 'stage0 mengompilasi sumber C@ untuk menghasilkan compiler C@ pertama, stage1.'),
    (3, 'STAGE2', 50, 'stage1 mengompilasi sumber yang sama', 'stage1 mengompilasi sumber sama untuk menghasilkan compiler C@ kedua, stage2.'),
    (3, 'STAGE3', 51, 'stage2 mengompilasi sumber yang sama', 'stage2 mengompilasi sumber sama untuk menghasilkan stage3.'),
    (3, 'BYTE-IDENTICAL', 51, 'identik byte', 'stage3 harus identik byte dengan stage2, yakni fixed point pada L53 yang menunjukkan konsistensi compiler dengan dirinya sendiri, bukan bukti bebas bug atau safety universal.'),
    (3, 'SAME-SUITE', 59, 'stage0, stage1, dan stage2; hasilnya harus sama', 'Suite sama dijalankan pada stage0, stage1 dan stage2 dengan hasil sama.'),
    (3, 'LANGUAGE-FINDINGS', 60, 'bukan tambalan mendadak', 'Kekurangan fitur saat self-hosting dicatat sebagai temuan tertunda atau edition berikutnya, bukan tambalan mendadak.'),
    (3, 'CPP-RETENTION', 61, 'selama beberapa rilis', 'Compiler C++ dipertahankan sampai stage3 identik selama beberapa rilis.'),
    (5, 'RACE-FREE-GOAL', 101, 'tanpa biaya runtime tambahan', 'Tujuan threading adalah bebas data race oleh sistem tipe tanpa biaya runtime tambahan; ini target desain, bukan bukti tercapai.'),
    (5, 'STRUCTURAL-DERIVATION', 105, 'diturunkan otomatis dari struktur tipe', 'Kontrak thread diturunkan otomatis dari struktur tipe; nama Send/Sync tetap owner PRD:20.'),
    (5, 'SEND-TRANSFER', 109, 'dipindahkan', 'Send berarti nilai boleh dipindahkan ke thread lain; keputusan nama tetap G2-PRD-20-SEND.'),
    (5, 'SYNC-SHARING', 110, 'dipakai bersamaan', 'Sync berarti ref ke nilai boleh dipakai bersama dari beberapa thread; keputusan nama tetap G2-PRD-20-SYNC.'),
    (5, 'SPAWN-SEND', 130, 'argumen bertipe `Send`', 'spawn hanya menerima argumen bertipe Send.'),
    (5, 'SCOPED-BORROW', 130, 'scope` yang akan menunggu', 'Pinjaman pada spawn terikat ke scope yang menunggu selesainya thread.'),
    (5, 'MUTABLE-EXCLUSION', 131, 'Dua thread tidak boleh', 'Dua thread tidak boleh memegang ref mut ke data sama; ini perluasan lintas spawn dari G2-PRD-9-MUTABLE-OVERLAP.'),
    (5, 'DATAFLOW-SPAWN', 131, 'dataflow (v0.7) diperluas', 'Borrow checker dataflow diperluas melintasi spawn; aturan liveness dasar tetap G2-PRD-9-DATAFLOW.'),
    (5, 'STATIC-SYNC', 132, 'tanpa tipe `Sync` = error', 'static mutable tanpa tipe Sync ditolak.'),
    (5, 'CHILD-DEFECT', 133, 'menghentikan seluruh program', 'Defect pada thread anak menghentikan seluruh program; alternatif join mengembalikan Result hanya pertimbangan nanti.'),
    (5, 'THREAD-RUNTIME', 135, 'pthread atau setara', 'Thread dilaksanakan lewat runtime pthread atau setara.'),
    (5, 'ATOMIC-LLVM', 135, '`atomicrmw`, `cmpxchg`, dan `fence`', 'Atomik memakai LLVM atomicrmw, cmpxchg dan fence.'),
    (5, 'SEQ-CST-DEFAULT', 135, 'default **seq_cst**', 'Urutan memori default adalah seq_cst.'),
    (5, 'EXPLICIT-ORDERING', 145, 'ordering lemah hanya eksplisit dan terdokumentasi', 'Ordering lebih lemah hanya eksplisit dan terdokumentasi; L135 menyebut acquire/release untuk kebutuhan kecepatan.'),
    (5, 'SPAWN-CLOSURE', 137, '`spawn` menerima closure', 'spawn menerima closure; keberadaan dan bentuk dasar closure tetap owner PRD/PLAN, bukan proposal baru.'),
    (5, 'OWNED-CAPTURE-MOVE', 137, 'menangkap nilai milik wajib `move`', 'Closure untuk spawn yang menangkap nilai milik wajib move; arti move dasar tetap G2-PRD-20-CLOSURE-MOVE.'),
    (5, 'CAPTURE-SEND', 137, 'semua tangkapannya `Send`', 'Closure hanya Send jika semua tangkapannya Send.'),
    (5, 'NEGATIVE-MUTABLE', 143, 'dua `ref mut`', 'Suite negatif threading mencakup dua ref mut lintas thread ke data sama.'),
    (5, 'NEGATIVE-NON-SYNC', 143, '`ref` non-`Sync` lintas thread', 'Suite negatif threading mencakup ref non-Sync lintas thread.'),
    (5, 'NEGATIVE-STATIC', 143, '`static mut` telanjang', 'Suite negatif threading mencakup static mut telanjang.'),
    (5, 'TSAN', 143, 'ThreadSanitizer pada runtime', 'Runtime threading diuji memakai ThreadSanitizer.'),
    (5, 'SCHEDULE-EXPLORATION', 144, 'schedule exploration', 'Primitif inti diuji dengan eksplorasi urutan eksekusi thread.'),
    (6, 'CHANNEL-SEND', 159, '`T: Send`', 'channel[T] dibatasi pada T: Send.'),
    (6, 'MOVE-AFTER-SEND', 160, 'tidak bisa dipakai lagi oleh pengirim', 'Nilai yang dikirim tidak boleh dipakai lagi oleh pengirim; konteks channel, bukan aturan move umum baru.'),
    (6, 'RECEIVER-END', 156, 'berhenti saat semua pengirim selesai', 'Contoh channel mengakhiri scan receiver saat seluruh pengirim selesai.'),
    (6, 'DEADLOCK-LIMIT', 161, 'deadlock tidak bisa dicegah oleh tipe', 'Sistem tipe channel tidak diklaim mencegah deadlock.'),
    (6, 'CAPACITY', 161, 'channel berkapasitas', 'Mitigasi channel menyediakan kapasitas.'),
    (6, 'TRY-SEND', 161, '`try_send`', 'Mitigasi channel menyediakan try_send tanpa menetapkan signature atau hasil baru.'),
    (6, 'TRY-RECV', 161, '`try_recv`', 'Mitigasi channel menyediakan try_recv tanpa menetapkan signature atau hasil baru.'),
    (6, 'SAFE-PATTERNS-DOC', 161, 'dokumentasi pola yang aman', 'Mitigasi deadlock mencakup dokumentasi pola channel yang aman.'),
    (7, 'CORE', 169, '`core` (tanpa alokasi, tanpa OS)', 'core tidak memakai alokasi maupun OS; program embedded hanya memakai core.'),
    (7, 'STD', 169, '`std` (butuh OS dan heap)', 'std adalah lapisan yang membutuhkan OS dan heap.'),
    (7, 'LLVM-TRIPLE', 170, 'triple LLVM', 'Target embedded ditentukan lewat triple LLVM, dengan Cortex-M/RISC-V sebagai contoh, bukan jaminan target diterima.'),
    (7, 'BARE-PROFILE', 171, '`claw build --profile bare`', 'claw build --profile bare tidak memakai thread runtime.'),
    (7, 'RAW-REGISTERS', 171, 'akses register lewat `raw`', 'Akses register pada profil bare memakai raw.'),
    (7, 'INTERRUPT-HANDLER', 171, 'penangan interupsi sebagai fungsi khusus', 'Penangan interupsi pada profil bare berupa fungsi khusus, tanpa signature baru yang disimpulkan.'),
    (7, 'ARENA-OFFICIAL-STAGE', 172, 'Arena resmi pertama kali di sini', 'Arena resmi ditargetkan pertama kali pada profil embedded; eksperimental dan pengecualian gerbang v1 tetap owner PLAN.'),
    (7, 'ARENA-FIXED-POOL', 172, 'pool memori tetap di atas buffer yang kamu sediakan (tanpa heap)', 'Arena merupakan pool tetap tanpa heap pada buffer yang disediakan pemakai.'),
    (7, 'ARENA-READ-ONLY', 172, 'data diisi sekali lalu hanya dibaca (`ref[a]`)', 'Data Arena diisi sekali lalu dibaca melalui ref[a].'),
    (7, 'ARENA-PLAIN-TYPES', 172, 'hanya tipe polos', 'Arena hanya menyimpan tipe polos.'),
    (7, 'ARENA-ALLOC-REF', 172, '`alloc` memakai `ref Arena`', 'alloc Arena memakai ref Arena.'),
    (7, 'ARENA-RESET-BORROW', 172, '`reset()` ditolak selagi ada referensi aktif', 'reset Arena ditolak selama referensi masih aktif.'),
    (7, 'ARENA-FULL-MAYBE', 172, 'Pool penuh mengembalikan `Maybe`, bukan crash.', 'Arena penuh mengembalikan Maybe, bukan crash.'),
    (7, 'ARENA-WRITABLE-DEFERRED', 172, 'Arena yang bisa ditulis menunggu `ref mut[s]` (v2.0)', 'Arena writable menunggu ref mut[s] pada usulan v2.0; larangan ref mut[s] v1 tetap G2-PRD-9-REFMUT.'),
    (7, 'STD-WRAPS-CORE', 173, '`std` hanya membungkus `core`', 'std hanya membungkus core sebagai satu sumber kebenaran tipe dasar.'),
    (7, 'CORE-EMULATOR-TESTS', 173, 'tes `core` dijalankan di emulator', 'Tes core dijalankan di emulator.'),
    (8, 'A-THREAD-CHANNEL-POLLING', 183, 'Thread + channel + polling I/O', 'Pilihan A adalah thread, channel dan polling I/O: sederhana tanpa fitur baru tetapi kurang efisien untuk puluhan ribu koneksi; belum menjadi keputusan.'),
    (8, 'B-ASYNC-AWAIT', 184, '`async/await` (mesin keadaan)', 'Pilihan B adalah async/await mesin keadaan: efisien untuk I/O dengan pewarnaan fungsi dan compiler lebih rumit; belum menjadi keputusan.'),
    (8, 'C-GREEN-THREADS', 185, 'Task ringan (*green threads*)', 'Pilihan C adalah green threads runtime tanpa pewarnaan, dengan runtime lebih berat dan stack per task; belum menjadi keputusan.'),
    (8, 'RECOMMEND-A', 187, '**Rekomendasi:** mulai dari A.', 'Mulai dari A hanya rekomendasi, bukan ratifikasi pilihan concurrency.'),
    (8, 'BENCHMARK-GATE', 187, 'benchmark server menunjukkan thread per koneksi tidak cukup', 'B atau C hanya dipertimbangkan bila benchmark server menunjukkan thread per koneksi tidak cukup.'),
    (8, 'NO-DECISION-WITHOUT-DATA', 187, 'Keputusan ini ditunda sampai ada data.', 'Keputusan async ditunda sampai ada data; tidak ada implementasi async yang dikreditkan.'),
    (9, 'BORROW-FORMAL-MODEL', 195, 'Model kecil yang diperiksa formal', 'Borrow checker memiliki model kecil yang diperiksa formal.'),
    (9, 'BORROW-DIFFERENTIAL', 195, 'differential testing terhadap referensi', 'Borrow checker diuji differential terhadap referensi.'),
    (9, 'BORROW-RANDOM-SAFETY', 195, 'Tidak ada program tak aman yang lolos pada korpus uji acak', 'Gerbang verifikasi adalah tidak ada program tak aman lolos pada korpus uji acak; bukan klaim pembuktian universal.'),
    (9, 'REFMUT-SCOPED', 196, 'Pinjaman mutable ber-scope', 'ref mut[s] mutable ber-scope adalah usulan pasca-v1; larangan v1 tetap G2-PRD-9-REFMUT.'),
    (9, 'REFMUT-EXCLUSIVITY', 196, 'Bukti eksklusivitas', 'Usulan ref mut[s] memerlukan bukti eksklusivitas.'),
    (9, 'REFMUT-NEGATIVES', 196, 'tes negatif lengkap', 'Usulan ref mut[s] memerlukan tes negatif lengkap.'),
    (9, 'SIMD-LLVM', 197, 'memetakan ke vektor LLVM', 'Tipe SIMD seperti Vec4[Float32] dipetakan ke vektor LLVM.'),
    (9, 'SIMD-BENCHMARK', 197, 'Benchmark numerik', 'SIMD memiliki gerbang benchmark numerik.'),
    (9, 'BACKEND-TARGETS', 198, 'WASM, backend lain lewat LLVM', 'Backend tambahan mencakup WASM dan backend lain lewat LLVM.'),
    (9, 'BACKEND-SUITE-PARITY', 198, 'Suite tes sama di semua target', 'Backend tambahan wajib menjalankan suite tes yang sama pada semua target.'),
    (9, 'GPU-EXPLORATION', 199, 'Eksplorasi saja', 'GPU/akselerator hanya eksplorasi.'),
    (9, 'GPU-NEED-GATE', 199, 'kebutuhan nyata dari proyek AI', 'Eksplorasi GPU/akselerator hanya bila ada kebutuhan nyata dari proyek AI.'),
    (10, 'CONVERT-DESIGN', 207, 'desain tertulis', 'Kontrak Convert tertunda pada v1.x dan memerlukan desain tertulis; tidak ada konversi diam-diam tetap rujukan PRD:5.'),
    (10, 'CONVERT-NEGATIVES', 207, 'tes negatif', 'Kontrak Convert tertunda memerlukan tes negatif.'),
    (10, 'DEFAULT-TYPE-LIST', 208, 'daftar tipe', 'Kontrak Default tertunda memerlukan daftar tipe; nama dan API dasar tetap owner PRD:20/10.'),
    (10, 'DEFAULT-GENERIC', 208, 'perilaku generik', 'Kontrak Default tertunda memerlukan perilaku generik yang ditentukan.'),
    (10, 'OPEN-SHAPE-GATE', 209, 'lint + inlay hint terbukti tidak cukup', 'open shape tetap tidak ada; usulan v1.x hanya bila lint dan inlay hint terbukti tidak cukup, melengkapi konteks mitigasi PRD:1.'),
    (10, 'FUNCTION-CONST-GENERIC', 210, 'kebutuhan nyata dari pustaka', 'Const generic fungsi fn f[N] tertunda v1.x; sementara gunakan Span[T], dan perlu kebutuhan nyata dari pustaka.'),
    (10, 'USER-ANNOTATION-GATE', 212, 'mesin macro selesai', 'Annotation pengguna menunggu mesin macro selesai pada usulan v1.3; annotation bawaan v1 tetap owner PRD:15.'),
    (10, 'QUEUE-CONSISTENCY', 214, 'konsistensi dengan `vec`/`set`/`map`', 'Literal Queue tertunda v1.x mempunyai syarat konsistensi vec/set/map; status Queue.new tetap G2-PRD-2-QUEUE.'),
    (10, 'NARROW-CONVERSION-TABLE-TESTS', 215, 'tes tabel konversi', 'Keputusan narrowing yang sudah ratifikasi mempunyai syarat tes tabel konversi; semantik tetap owner G2-PRD-20-NARROW-*.'),
    (10, 'ARENA-COMPILER-AST', 217, 'data waktu kompilasi', 'Arena untuk AST compiler sementara memakai Anchor atau vec + indeks; keputusan usulan v1.2 berdasarkan data waktu kompilasi.'),
]

PREFIX = 'G2-ROADMAP-'

def atom_id(spec):
    return f'{PREFIX}{spec[0]}-{spec[1]}'


def validate_roadmap_inventory(root: Path, rows):
    errors = []
    row_map = {cells[0]: cells for _, cells in rows if len(cells) == 9}
    lines = (root / 'cat-northstar/ROADMAP-v1-plus.md').read_text(encoding='utf-8').splitlines()
    for spec in SPECS:
        section, _, line, phrase, description = spec
        ident = atom_id(spec)
        if line > len(lines) or phrase not in lines[line - 1]:
            errors.append(f'roadmap inventory: original source clause changed or missing for {ident}')
        cells = row_map.get(ident)
        if cells is None:
            errors.append(f'roadmap inventory: missing canonical owner {ident}')
            continue
        if cells[1] != f'ROADMAP:{section}' or cells[3] != '[diusulkan]':
            errors.append(f'roadmap inventory: {ident} must retain its ROADMAP section and proposal label')
        if description not in cells[4]:
            errors.append(f'roadmap inventory: {ident} bounded scope does not match inventory requirement')
    return errors
