# 2026-10-12 — identitas receiver builtin member

## Masalah yang direproduksi

Sema menerima shadowing lokal pada scope leksikal turunan, tetapi OIR mereduksi receiver method builtin menjadi teks `receiver.member`. LLVM kemudian mencari receiver dengan nama tanpa binding key. Repro `Anchor.get()` di scope bersarang mencetak `99` lalu `99`; nilai yang benar adalah `20` lalu `99`.

## Perubahan

- `OirCallInst` menyimpan `OirValue` receiver builtin secara eksplisit, termasuk binding key semantik.
- Identitas receiver diteruskan ke LIR. LLVM memakai nilai receiver tersebut langsung melalui lookup yang memahami key, termasuk saat memperoleh alamat receiver; LLVM tidak lagi membangun ulang receiver bernama tanpa key.
- Teks callee tetap dipertahankan untuk diagnostik dan dispatch; perubahan ini tidak mengubah kontrak builtin atau dispatch Arena.

## Regresi dan verifikasi

`claw/tests/native/fixtures/revise_builtin_member_receiver_shadowing.cat` dikunci pada output native persis:

```text
20
99
```

Frontend runner memeriksa fixture dapat diterima; native runner membangun executable dan membandingkan seluruh output. Sebelum perubahan, repro diterima sema tetapi native menghasilkan `99\n99`; setelah perubahan, native menghasilkan `20\n99`.

## Batas

Tidak ada perubahan PRD, kontrak/runtime/fixture/artifact Arena, maupun perilaku semantik shadowing. Deklarasi duplikat pada scope yang sama tetap ditolak. Tidak ada stage, commit, push, atau PR yang dibuat.
