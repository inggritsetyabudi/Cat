# Batas aman payload Anchor bersarang (2026-10-12)

Status: nested Anchor payload ditolak secara konservatif sampai kontrak destruction rekursif dan drop glue dirancang. PRD kanonis tidak mendefinisikan semantik payload Anchor bersarang; tidak ada perubahan pada Arena atau status PRD.

## Temuan baseline

Sebelum patch awal, program berikut lolos `claw check` dan dapat dibangun:

```cat
fn main() {
    val outer = Anchor.new(Anchor.new(77))
}
```

Native ASan/LSan mereproduksi `Direct leak of 4 byte(s) in 1 object(s)`, berasal dari `claw.runtime.anchor.alloc` pada `claw/runtime/native_runtime.c:62`. OIR membuat `Anchor[Int32]` bagian dalam, menyimpannya sebagai payload Anchor luar, lalu hanya mengeluarkan `drop outer`. Backend `LirDropInst` Anchor hanya memanggil `claw.runtime.anchor.free` untuk pointer luar (`claw/src/codegen/llvm_emit.cpp:242–248`); runtime `free` tidak menjalankan destructor payload (`native_runtime.c:70–72`).

Review berikutnya menemukan bypass type-position tanpa kebocoran yang direproduksi: `shape AnchorBox[T] { value: Anchor[T] }` dan `fn accept(box: AnchorBox[Anchor[Int32]]) {}` lolos `check`, OIR, dan native build. Pada jalur ini field generik baru disubstitusi setelah validator yang sebelumnya hanya memeriksa root `Anchor`. Meski tidak ada leak dari fixture type-only tersebut, nested Anchor tetap harus ditolak pada semua specialization yang relevan sesuai kebijakan keselamatan yang dipilih.

## Perubahan dan batas

Validasi sema kini menelusuri tipe yang sudah di-resolve, termasuk parameter tipe dan field/payload Shape/Choice yang disubstitusi dengan `substituteType`. Karena itu specialization `AnchorBox[Anchor[Int32]]` menemukan field konkret `Anchor[Anchor[Int32]]` dan ditolak. Validator yang sama juga digunakan untuk payload `Anchor.new` yang diinferensikan dari sebuah Shape, sehingga `Anchor.new(box)` ditolak saat `box` mempunyai field Anchor. Diagnostic menjelaskan bahwa drop Anchor luar hanya membebaskan storage-nya sendiri dan recursive payload destruction belum didukung.

Payload valid seperti `AnchorBox[Str]` dan `Anchor[Int32]` tetap diterima. Perubahan ini tidak menambahkan recursive destructor atau ABI/runtime behavior baru. Dukungan nested ownership ditunda sampai kontrak desain dan drop glue diadopsi secara eksplisit. Perubahan ini tidak menyentuh Arena.

## Regression dan verifikasi

`claw/tests/frontend/fixtures/revise_bad_anchor_nested_payload.cat` menjaga negative check/OIR regression untuk `Anchor.new(Anchor.new(77))`; `revise_bad_anchor_nested_type.cat` menjaga tipe eksplisit `Anchor[Anchor[Int32]]`; `revise_bad_anchor_nested_shape_specialization.cat` menjaga bypass type-position `AnchorBox[Anchor[Int32]]`; dan `revise_bad_anchor_nested_shape_payload.cat` menjaga `Anchor.new(box)` untuk Shape payload. Native harness memastikan build gagal tanpa executable untuk constructor dan kedua jalur Shape. `claw/tests/native/fixtures/revise_anchor_box_specialization.cat` memastikan `AnchorBox[Str]`, `Anchor[Int32]`, dan constructor payload plain tetap lolos check, native build, serta execution.

Clean CMake configure/build dan `bash claw/tests/run_all_tests.sh` lulus pada fresh disposable copy setelah seluruh perubahan dan regression di atas. Frontend, OIR, native build-fail/no-binary, backend, dan sanitizer suites yang ada semuanya lulus. `git diff --check` serta `bash -n` pada test harness juga lulus.
