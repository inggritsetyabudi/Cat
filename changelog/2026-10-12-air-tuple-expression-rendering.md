# Rendering ekspresi tuple di AIR (2026-10-12)

Scope tooling-only: renderer AIR menampilkan isi `TupleExpr` secara rekursif. Parser, sema, ownership, dan lowering tidak diubah. §8 tetap parsial; slice ini tidak menyelesaikan dukungan tuple generic, batas destructuring owned, atau pekerjaan tuple lainnya. Arena tidak disentuh.

## Baseline

Sebelum perubahan, `claw air claw/tests/native/fixtures/revise_tuples.cat` menampilkan `<expr>` untuk tuple return, singleton, tuple nested, serta tuple values di cabang `if`/`pick`. Nilai dan aritas tuple tidak dapat dibaca dari AIR walau tipe tuple sudah tercetak.

## Perubahan

`AirEmitter::emitExprValue` kini merender setiap elemen `TupleExpr` secara rekursif:

- tuple multi-elemen: `(40, 2)`;
- singleton: `(42,)`;
- nested: `((1, 2), 40)`;
- tuple kosong/Unit: `()`.

Karena renderer `if` dan `pick` yang ada sudah memanggil renderer expression rekursif, isi tuple di cabang tersebut kini terlihat tanpa mengubah representasi kontrol. Grouping scalar tetap memakai format renderer yang sudah ada, misalnya `(20 + 22)`.

## Regression dan verifikasi

Golden baru `claw/tests/frontend/fixtures/revise_tuples.air.golden` merekam seluruh output AIR dari fixture Cat `revise_tuples.cat`. Assertion di frontend harness memastikan output tidak berisi `<expr>`, nilai dan aritas tuple terlihat untuk return/nested/singleton serta branch `if`/`pick`, dan format grouping scalar tetap stabil. Golden juga mengunci output non-tuple di fixture agar tidak berubah.

Clean configure/build dan `bash claw/tests/run_all_tests.sh` dijalankan pada disposable copy setelah perubahan; frontend/AIR runner dan full suite lulus. Output `claw air` nyata untuk fixture diperiksa langsung dan tidak lagi memiliki fallback `<expr>`.
