# Fondasi ownership `Str`/`String` (2026-10-17)

Implementasi fondasi kontrak string borrowed/owned yang telah disepakati, tanpa anotasi lifetime pada source.

## Kontrak dan semantik

- `Str`/`str` adalah view UTF-8 borrowed `{ptr, len}` (16 byte pada target 64-bit); `String`/`string` adalah storage owned immutable dan move-only `{ptr, len, cap}` (24 byte pada target 64-bit).
- Literal string tetap berupa `Str` statis read-only. `String.from(str)` melakukan copy dan alokasi eksplisit; `.as_str()` meminjam dari owner; `.clone()` membuat salinan owned. Method `len()` tersedia untuk string.
- Ownership checker melacak identitas binding secara leksikal, termasuk shadowing, dan menolak use-after-move, view yang lolos dari owner lokal, serta pemindahan/pengubahan owner saat view masih hidup. Pemakaian langsung atas view milik owner temporary diizinkan sampai akhir full-expression; menyimpannya, mengembalikannya, atau melewatkannya melalui join nilai `if` ditolak.
- Borrowed `Str` tidak boleh menjadi field shape normal. Generic storage yang belum memiliki propagasi borrow dan element-drop glue menolak `Str` maupun `String`; payload `String` di `shape`, `choice`, dan `Anchor` didukung melalui recursive drop glue. OOM pada alokasi owned berakhir deterministik.
- Diagnostik ownership berwarna, menjelaskan alasan dan hint, serta mengikuti `NO_COLOR` dan kebijakan no-ANSI Windows.

## ABI dan regresi

- Win64 memakai pointer parameter untuk borrowed `Span` dan `Str` 16-byte, konsisten pada definisi callee dan direct call. SysV x64 tetap menggunakan aggregate-by-value.
- Native C harness menguji `Span` serta `Str` lewat forwarding C@→C@ dan panggilan C lintas ABI. Cabang harness memakai deklarasi pointer pada Windows dan deklarasi aggregate-by-value pada SysV.
- Fixture frontend/OIR mencakup alias, pemindahan, shadowing, lifetime scope, return/escape, payload generik, serta penolakan temporary borrow yang tidak aman. Fixture native mencakup clone, cleanup temporary, overwrite, UTF-8 view, nested shape/choice, `Result`, `Anchor`, serta output dan sanitizer.

## Validasi

- Full Linux Release suite pada snapshot disposable lulus: PRD doc-test, frontend, OIR golden, LLVM backend, native, dan CLI/PTy.
- Regresi native ownership lulus dengan ASan/UBSan/LSan, termasuk cleanup temporary dan recursive drop.
- Targeted Linux C harness menghasilkan output tepat `9\n42\n42\n3` dan mempertahankan signature aggregate SysV; kedua cabang C harness lolos pemeriksaan sintaks.
- Assertion signature Win64 disertakan untuk Windows UCRT64 CI. Validasi lokal pada sesi ini tidak menjalankan executable Windows.

F-string/interpolasi, string builder, borrowed field pada shape normal, dan generic storage untuk `Str`/`String` tetap di luar cakupan fondasi ini.
