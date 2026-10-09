# 2026-10-09: static Global Constants Implementation

## Features Implemented:
1. **Parser & AST**:
   - Menambahkan `StaticDecl` AST node.
   - Parsing `static NAME: Type = expr` di module level.
   - Penolakan sintaks `var static` dan penolakan deklarasi `static` lokal di dalam fungsi.

2. **Semantic Analysis & Borrow Checking**:
   - Pendaftaran identifier static ke scope symbol table sebagai global constant.
   - Validasi bahwa initializer static wajib berupa compile-time literal expression.
   - Pengecualian lifetime borrow untuk referensi ke variabel `static`: referensi ke variabel `static` selalu valid seumur program (unbounded/static lifetime) dan dapat di-return tanpa dibatasi oleh scope parameter fungsi.

3. **Intermediate Representations (OIR & LIR)**:
   - Menambahkan `OirStatic` dan `LirStatic` declaration nodes.
   - Menurunkan (lower) `StaticDecl` ke representasi IR secara lengkap.

4. **LLVM Codegen**:
   - Emisi LLVM global constants (misal `@mod::NAME = internal constant ...`).
   - Penanganan pembacaan nilai dan referensi alamat ke simbol global static saat lowering ekspresi.

5. **Tests**:
   - `claw/tests/native/fixtures/revise_static.cat`: Pengujian positif native binary atas akses static konstanta integer, string, boolean, dan return `ref static`.
   - `claw/tests/frontend/fixtures/revise_bad_static.cat`: Pengujian negatif penolakan `var static` dan static dalam fungsi.
   - Semua test suite (`run_all_tests.sh`) 100% PASS.
