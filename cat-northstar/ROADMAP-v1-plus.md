# Roadmap C@ setelah v1

Semua isi dokumen ini berstatus **usulan desain**. Belum ada yang diputuskan kecuali: `rewrite` (macro) ada di v1.x, dan self-hosting dimulai setelah v1 terpenuhi. Dokumen ini terpisah dari *Perubahan PRD v0.1 sampai v1.0* supaya v1 tetap kecil dan bisa diuji.

---

## 0. Aturan main untuk setiap fitur pasca-v1

Fitur baru hanya masuk bila lolos lima syarat. Ini juga yang menjaga risiko tetap kecil.

1. **Desain tertulis lebih dulu**, termasuk daftar apa yang sengaja tidak dikerjakan.
2. **Tes negatif**: program yang harus ditolak (khusus Safe).
3. **Kriteria Fast/Safe/Simple** yang terukur untuk fitur itu.
4. **Lolos bootstrap**: compiler C@ yang membangun dirinya sendiri tetap menghasilkan biner identik.
5. **Edition:** setelah v1, perubahan yang merusak kode lama hanya lewat *edition* (penanda versi bahasa per proyek, mis. `edition = "2027"` di konfigurasi). Kode lama tetap dikompilasi dengan aturan lamanya.

---

## 1. Peta versi

| Versi | Fitur | Mengapa di urutan ini |
| --- | --- | --- |
| v1.1 | Pustaka standar dan tooling (`fmt`, `doc`, `test`, language server) | Prasyarat self-hosting; menguji "Simple" |
| v1.2 | **Self-hosting** (bootstrap) | Ujian akhir slogan v1 |
| v1.3 | **`rewrite` (macro)** | Ekspander ditulis dengan C@ sendiri |
| v1.4 | **Threading** (thread terstruktur, mutex, atomik) | Fondasi concurrency yang aman |
| v1.5 | **Channel** (message passing) | Memindahkan kepemilikan antar thread |
| v1.6 | **Profil embedded** (`core`, tanpa OS) | Target mikrokontroler |
| v1.7 | **Keputusan async** | Hanya dikerjakan bila terbukti perlu |
| v2.0 | Verifikasi formal, `ref mut[s]`, SIMD, backend tambahan | Penguatan jangka panjang |

---

## 2. v1.1 Pustaka standar dan tooling

- **Pustaka:** file I/O, string builder, `vec`, `map`, `set`, waktu, argumen proses, `Maybe`/`Result` helper.
- **Tooling:** `claw fmt` (satu gaya, tanpa opsi, supaya Simple), `claw doc` (dari `///` pada item `share`), `claw test` (dari `@test`), language server (inlay hint nama parameter, yang menutup risiko "tanpa argumen bernama").
- **Gerbang:** compiler C@ bisa ditulis hanya dengan pustaka ini.

---

## 3. v1.2 Self-hosting

**Tahap bootstrap** (kompilasi bertahap sampai hasilnya stabil):

| Tahap | Dibangun oleh | Hasil |
| --- | --- | --- |
| stage0 | compiler C++ saat ini | biner awal |
| stage1 | stage0 mengompilasi sumber C@ | compiler C@ pertama |
| stage2 | stage1 mengompilasi sumber yang sama | compiler C@ kedua |
| stage3 | stage2 mengompilasi sumber yang sama | harus **identik byte** dengan stage2 |

Jika stage2 dan stage3 identik (*fixed point*), compiler terbukti konsisten dengan dirinya sendiri.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Bug di stage0 menurun ke stage1 | Suite tes yang sama dijalankan di stage0, stage1, dan stage2; hasilnya harus sama |
| Fitur bahasa yang kurang terasa saat menulis compiler | Catat sebagai temuan; masuk daftar tertunda atau edition berikutnya, bukan tambalan mendadak |
| Compiler C++ lama dibuang terlalu cepat | Dipertahankan sampai stage3 identik selama beberapa rilis |

---

## 4. v1.3 `rewrite` (macro)

**Tujuan:** menghasilkan kode saat kompilasi tanpa mengorbankan keterbacaan.

```cat proposal
rewrite repeat!(n: Int32, body: Block) {
    scan _i over 0..n { body }
}

fn main() -> Int32 {
    repeat!(3, { println("hi") })
    return 0
}
```

**Aturan usulan**

- Ekspansi terjadi **sebelum** pengecekan tipe; hasilnya AST biasa yang lalu dicek seperti kode tulisan tangan. Jadi hasil macro tidak punya jalan pintas Safe.
- **Hygiene** (kebersihan nama): nama yang dibuat di dalam macro tidak bisa bentrok dengan nama di tempat pemanggilan.
- Macro **tidak bisa** melakukan I/O atau memanggil kode luar saat ekspansi.
- **Batas ekspansi** (kedalaman dan jumlah node) supaya macro rekursif tidak berputar selamanya.
- Pesan error menunjuk ke **tempat pemanggilan** dan menyebut ekspansinya, bukan ke tengah kode hasil macro.
- Annotation buatan pengguna (`@derive` dll.) dibangun di atas mesin yang sama.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Macro menyembunyikan bug | `claw expand` menampilkan hasil ekspansi; lint untuk macro yang menghasilkan lebih dari N node |
| Pesan error membingungkan | Setiap node hasil macro membawa lokasi asal (*source span*) |
| Mesin ekspansi menjadi bahasa kedua yang rumit | Ekspander hanya memanipulasi AST; tidak ada logika sembarang |

---

## 5. v1.4 Threading

**Tujuan:** thread yang **bebas data race** oleh sistem tipe, tanpa biaya runtime tambahan.

**Pondasi yang sudah ada:** `ref mut` itu eksklusif, dan PRD sudah melarang global mutable tanpa pembungkus aman (contoh `Mutex`).

**Dua kontrak penanda** (diturunkan otomatis dari struktur tipe; nama final diputuskan nanti agar tidak bentrok dengan kata `share`):

| Kontrak | Arti |
| --- | --- |
| `Send` | nilai boleh **dipindahkan** ke thread lain |
| `Sync` | `ref` ke nilai boleh **dipakai bersamaan** dari beberapa thread |

**Thread terstruktur**, memakai `scope` yang sudah ada sehingga pinjaman ke stack aman:

```cat proposal
fn sum_parts(data: ref Span[Int32]) -> Int32 {
    val (left, right) = data.split_at(data.len() / 2)
    scope s {
        val h = spawn[s](|| sum(left))   // closure meminjam 'left'; scope menunggu thread selesai
        val b = sum(right)
        val a = h.join()
        return a + b
    }
}

static counter: Mutex[Int32] = Mutex.new(0)     // global mutable hanya lewat tipe Sync
```

**Pemeriksaan compiler**

- `spawn` hanya menerima argumen bertipe `Send`; pinjaman harus terikat ke `scope` yang akan menunggu.
- Dua thread tidak boleh memegang `ref mut` ke data yang sama; borrow checker dataflow (v0.7) diperluas melintasi `spawn`.
- `static` mutable tanpa tipe `Sync` = error.
- Defect di thread anak menghentikan seluruh program (aturan paling sederhana dan aman; alternatif `join` mengembalikan `Result` bisa dipertimbangkan nanti).

**LLVM:** thread lewat runtime (pthread atau setara). Atomik memakai `atomicrmw`, `cmpxchg`, dan `fence`. Urutan memori default **seq_cst** (paling mudah dan aman); `acquire`/`release` eksplisit untuk kode yang butuh kecepatan.

**Closure:** `spawn` menerima closure (diputuskan: C@ punya closure). Closure yang menangkap nilai milik wajib `move`, dan hanya `Send` bila semua tangkapannya `Send`.

**Risiko → penutup**

| Risiko | Penutup |
| --- | --- |
| Data race lolos | Tes negatif (dua `ref mut`, `ref` non-`Sync` lintas thread, `static mut` telanjang) + ThreadSanitizer pada runtime |
| Bug urutan eksekusi sulit direproduksi | Tes yang menjelajahi urutan eksekusi thread (*schedule exploration*) untuk primitif inti |
| `seq_cst` lebih lambat dari perlu | Dipakai sebagai default; ordering lemah hanya eksplisit dan terdokumentasi |

---

## 6. v1.5 Channel (message passing)

Memindahkan nilai antar thread **memindahkan kepemilikannya**, sehingga tidak ada data yang dipakai bersama.

```cat proposal
val (tx, rx) = channel[Int32]()
spawn[s](producer, tx)               // tx dipindahkan ke producer
scan msg over rx { println(msg) }    // berhenti saat semua pengirim selesai
```

- `channel[T]` hanya untuk `T: Send`.
- Nilai yang sudah dikirim tidak bisa dipakai lagi oleh pengirim (aturan *move* yang sudah ada).
- **Risiko:** deadlock tidak bisa dicegah oleh tipe. **Penutup:** channel berkapasitas dengan `try_send`/`try_recv`, plus dokumentasi pola yang aman.

---

## 7. v1.6 Profil embedded

**Tujuan:** C@ dipakai tanpa OS dan tanpa alokasi heap (mikrokontroler).

- **Dua lapis pustaka:** `core` (tanpa alokasi, tanpa OS) dan `std` (butuh OS dan heap). Program embedded hanya memakai `core`.
- Target lewat triple LLVM (misalnya ARM Cortex-M atau RISC-V).
- `claw build --profile bare`: tanpa thread runtime; akses register lewat `raw`; penangan interupsi sebagai fungsi khusus.
- **Arena resmi pertama kali di sini**, sebagai pool memori tetap di atas buffer yang kamu sediakan (tanpa heap). Di v1 Arena berlabel eksperimental. Modelnya: data diisi sekali lalu hanya dibaca (`ref[a]`), hanya tipe polos, dan `alloc` memakai `ref Arena` sehingga `reset()` ditolak selagi ada referensi aktif. Pool penuh mengembalikan `Maybe`, bukan crash. Arena yang bisa ditulis menunggu `ref mut[s]` (v2.0).
- **Risiko:** pustaka terbelah dua. **Penutup:** `std` hanya membungkus `core`, jadi satu sumber kebenaran untuk tipe dasar; tes `core` dijalankan di emulator.

---

## 8. v1.7 Keputusan async

`async/await` menambah "pewarnaan fungsi" (fungsi async hanya bisa dipanggil dari async), yang bertentangan dengan Simple. Tiga pilihan:

| Pilihan | Keunggulan | Kelemahan |
| --- | --- | --- |
| A. Thread + channel + polling I/O | Paling sederhana; tidak ada fitur baru | Kurang efisien untuk puluhan ribu koneksi |
| B. `async/await` (mesin keadaan) | Sangat efisien untuk I/O | Pewarnaan fungsi; kompiler lebih rumit |
| C. Task ringan (*green threads*) di runtime | Tanpa pewarnaan; ramah pengguna | Runtime lebih berat; tumpukan per task |

**Rekomendasi:** mulai dari A. Pindah ke B atau C **hanya bila** benchmark server menunjukkan thread per koneksi tidak cukup. Keputusan ini ditunda sampai ada data.

---

## 9. v2.0 Penguatan jangka panjang

| Item | Isi | Gerbang |
| --- | --- | --- |
| Verifikasi borrow checker | Model kecil yang diperiksa formal + differential testing terhadap referensi | Tidak ada program tak aman yang lolos pada korpus uji acak |
| `ref mut[s]` | Pinjaman mutable ber-scope | Bukti eksklusivitas, tes negatif lengkap |
| SIMD | Tipe vektor (`Vec4[Float32]` dll.) memetakan ke vektor LLVM | Benchmark numerik |
| Backend tambahan | WASM, backend lain lewat LLVM | Suite tes sama di semua target |
| GPU / akselerator | Eksplorasi saja | Hanya bila ada kebutuhan nyata dari proyek AI |

---

## 10. Daftar keputusan tertunda

| Item | Keputusan sementara | Target | Syarat |
| --- | --- | --- | --- |
| Kontrak `Convert` (konversi error di `try`) | Tidak ada konversi diam-diam | v1.x | desain tertulis + tes negatif |
| Kontrak `Default` | Tidak ada nol otomatis | v1.x | daftar tipe + perilaku generik |
| `open shape` (literal dari luar untuk data murni) | Tidak ada | v1.x | hanya bila lint + inlay hint terbukti tidak cukup |
| `fn f[N]` (const generic pada fungsi) | Pakai `Span[T]` | v1.x | kebutuhan nyata dari pustaka |
| Closure (fungsi anonim) | Diputuskan ada, sintaks mirip Rust, bentuk pendek dan blok | v0.5 | catatan `spawn` di bagian v1.4 |
| Annotation buatan pengguna | Hanya bawaan | v1.3 | mesin macro selesai |
| Nama kontrak thread | Diputuskan: `Send` dan `Sync` | v1.4 | tidak bentrok dengan `share` |
| Literal koleksi lain (`Queue`) | `Queue.new()` | v1.x | konsistensi dengan `vec`/`set`/`map` |
| `as` penyempitan | Diputuskan: defect di debug, terpotong di release; konversi aman lewat `to` yang mengembalikan `Maybe` | v0.5 | tes tabel konversi |
| Arena | Eksperimental di v1; resmi di v1.6 sebagai pool memori tetap (isi sekali, baca saja) | v1.6 | `alloc` memakai `ref Arena`; hanya tipe polos |
| Arena untuk AST compiler | Pakai `Anchor` atau `vec` + indeks | v1.2 | keputusan berdasarkan data waktu kompilasi |
