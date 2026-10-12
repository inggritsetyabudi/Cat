# 2026-10-12 — identitas binding untuk `try` dan payload `pick`

## Masalah yang direproduksi

Beberapa binder yang diterima sema belum memiliki identitas backend saat men-shadow nama luar:

- `val number = try produce()` dipetakan kembali ke `number` luar; native mencetak `99\n99`, bukan `7\n99`.
- Binder payload `pick` punya masalah yang sama. Untuk payload owned `Anchor`, lookup/drop yang salah dapat membebaskan slot luar dan menyebabkan double-free.

## Perubahan

- Hasil `try` dan binding kegagalan memperoleh backend key dari sema; key dibawa melalui ownership, OIR, LIR, dan materialisasi LLVM.
- Payload choice pada `pick` menyimpan key per binder. Materialisasi payload di LLVM menyimpan nilai, tipe, dan alamat di bawah key yang sama; ownership/drop memakai key tersebut.
- Daun binding pada pola tuple `pick` juga membawa key melalui AST, ownership, dan OIR, termasuk di ekspresi `pick`.
- Lookup key kosong tetap memakai nama sumber untuk binder non-shadow. Aturan duplicate nama pada scope yang sama tidak berubah.

## Regresi dan verifikasi

| Fixture | Cakupan | Output native yang diwajibkan |
| --- | --- | --- |
| `revise_try_result_shadowing.cat` | hasil `try` men-shadow binding luar | `7\n99` |
| `revise_pick_payload_shadowing.cat` | payload `pick` statement/expression dan pola tuple statement/expression | `7\n99\n8\n99\n12\n99\n14\n99` |
| `revise_pick_anchor_payload_shadowing.cat` | payload choice owned `Anchor`, cleanup branch, dan nilai luar | `inner\nouter` |
| `revise_bad_same_scope_duplicate_binding.cat` | duplicate local di scope yang sama | ditolak: `Duplicate variable declaration: repeated` |

Fixture Anchor juga dikompilasi dari LLVM IR dengan `clang -fsanitize=address,undefined,leak -O1 -g`, lalu dijalankan dengan ASan/UBSan/LSan aktif. Output `inner\nouter` cocok persis dan tidak ada diagnostik sanitizer, leak, atau kegagalan proses.

Fresh configure/build dan `claw/tests/run_all_tests.sh` lulus di disposable copy. Regresi local-shadowing biasa, nested scan, tuple destructuring/pick, serta Anchor cleanup yang sudah ada tetap hijau.

## Batas

PRD kanonis dan status §5/§7/§8 tidak diubah atau ditandai selesai. Arena — termasuk kontrak, runtime, fixture, dan artifact — tidak disentuh. Tidak ada stage, commit, push, atau PR yang dibuat.
