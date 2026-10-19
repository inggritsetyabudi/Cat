# PoC String/Str bounded melalui LLVM23 dan LLD

Pada branch PR36 yang sama, `--strings` membuka irisan aggregate/ownership
secara opt-in. `CLAW_ENABLE_LLVM_CPP_POC`, `CLAW_ENABLE_LLD_POC`, dan
`CLAW_ENABLE_RUNTIME_POC` tetap default OFF. Tidak ada perubahan pada frontend,
ownership, lowering/runtime/ABI produksi, PRD, versi, release, atau tag.

## Kontrak yang ditiru

PRD §12 tetap otoritatif: Str borrowed UTF-8 `{ptr,len}`, String owned affine
immutable `{ptr,len,cap}`, clone eksplisit. ABI mengikuti paired native oracle
pada `116299f0b09ab54b94545c3977396a705605315a`: Linux Str aggregate parameter,
Windows GNU/UCRT Str pointer parameter, return Str aggregate, String pointer
argument dan hidden result slot pertama dengan return void. Tidak menambahkan
atribut ABI yang tidak ada pada produksi.

Literal memakai decoder produksi. `String.from` mengalokasikan `max(len,1)` dan
copy tepat byte length; `.as_str` hanya view, `.clone` mengalokasikan salinan
independen dengan length/capacity yang sama. Drop yang sudah terdapat di LIR
memanggil `claw.runtime.string.free` dengan pointer owner. Tidak ada implicit
clone/realloc atau interpretasi terminator. Spill/slot di entry mencegah
pertumbuhan stack per iterasi; initialization tetap pada titik asal.

## Batas yang sengaja tidak dibuka

Adapter tetap menolak Result/choice/pick/try, generic collections, range/scan,
index/slice/range view, comparison String, arbitrary value phi, general ref/FFI,
shape/borrowed fields, Arena, f-string dan Show. Tidak ada analyzer ownership
kedua maupun inferensi provenance baru.

Uji sibling branch menemukan LIR produksi dapat memberi dua owner bernama sama
tanpa ID unik. Guard duplicate binding dipertahankan: bentuk itu adalah explicit
negative, bukan dianggap didukung. Branch dengan binding tidak ambigu, nested
tracked shadow, temporary, early return dan loop/skip/stop memiliki test sendiri.
Ini batas eksperimen, bukan perubahan semantik bahasa atau penolakan produksi.

## Bukti dan gate

Gate baru `llvm-cpp-poc-strings` menambah 19 program positif/38 alokasi observer
per run; membandingkan byte stdout/stderr/exit terhadap produksi O0/O2 native,
termasuk empty, NUL, multibyte, helper transfer/return slot, clone independence,
reverse cleanup, aliased views, local/parameter shadowing dan loop exits.
API test menambah 23 malformed aggregate ABI/builtin/binding negatives serta
penolakan tanpa mengganti object/IR valid sebelumnya. File lock mengunci source
41 kasus lama, 29 arithmetic programs, serta kontrak 14-case/63-allocation oracle.
Semua gate lama tetap dijalankan.

Runtime normal adalah archive prebuilt yang sama. Observer, OOM dan sanitizer
merupakan paket test-only terpisah yang disiapkan sekali sebelum semua emisi/
link per source. Hash/mtime frozen C source, object, archive dan manifest wajib
stabil. Setiap link per source hanya menerima object/archive/CRT/SDK melalui LLD,
dengan PATH tanpa compiler dan tanpa input `.cat`, `.ll` atau `.c`.

Observer memakai real malloc/free dan kontrol normal/leak/untracked-free.
Sanitizer Linux menginstrumentasi runtime C dan C host; archive sanitizer tidak
memuat registry pointer observer agar LSan tidak menyamarkan leak. Kontrol leak
LLD+LSan wajib gagal dengan laporan leak. Instruksi LLVM hasil adapter tidak
seluruhnya diinstrumentasi, sehingga hasil ini bukan bukti safety universal.
Windows menjalankan probe sanitizer runtime lengkap; unsupported/missing runtime
dilaporkan SKIP dengan diagnostic, tidak menggantikan native observer/byte gate.
OOM melalui malloc-failure test-only harus mencapai diagnostic/abort runtime asli;
missing archive dan genuine undefined println.slice harus gagal link.

Workflow native Linux Release/sanitizer-host dan Windows UCRT menyimpan evidence
tambahan. Suite produksi tetap pada checkout disposable, dengan oracle diagnostik
baseline lama tidak dilonggarkan. Hasil CI, reviewer fresh build/run exact SHA,
ukuran dan timing final harus dibaca dari evidence pada commit yang dinilai;
penambahan gate atau kelulusan Linux lokal bukan bukti Windows atau production-ready.
