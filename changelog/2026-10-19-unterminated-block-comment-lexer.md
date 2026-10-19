# Komentar blok tak tertutup ditolak sebelum AST/lowering (2026-10-19)

## Defect dan baseline

Pada main `2198eafb31a8bae7e8ff368a36a300525e45f052`, sumber berikut salah diterima oleh `check`, AIR/OIR/LLVM, dan native `build`; executable yang terbentuk juga keluar sukses:

```cat error: Unterminated block comment.
fn main() -> Int32 {
    return 0
}
/* unterminated
```

Penyebabnya adalah `Lexer::skipBlockComment()`: EOF menghentikan pemindaian tanpa melaporkan sisa kedalaman komentar. Program valid di depan komentar dapat lolos seluruh pipeline.

## Perbaikan terfokus

Lexer menyimpan lokasi pembuka komentar dan mengeluarkan `CommentError` dengan span dua byte `/*` bila pemindaian mencapai EOF sebelum komentar selesai. `Parser::parseFile()` memeriksa token komentar ini sebelum membentuk AST, sehingga kasus dalam declaration/expression juga tidak tertutup oleh pemulihan parser. Token unknown-character, string, dan Char mempertahankan penanganan sebelumnya.

Diagnostic yang diwajibkan adalah `error[lexer]: Unterminated block comment.` pada pembuka komentar, bukan EOF. Tidak ada perubahan grammar atau ratifikasi semantik nesting baru: perilaku nested-comment yang sudah ada pada implementasi baseline hanya dipertahankan. Tidak ada perubahan PRD, versi, tag, release, Arena, atau batas fitur lain.

## Regresi permanen

`claw/tests/frontend/run_lexer_comment_tests.sh` memanggil runner Python dan terpasang dalam `claw/tests/run_all_tests.sh` tanpa menghapus atau melemahkan tes lama.

- **13 sumber negatif × 13 mode:** `check`, `validate`, AIR/OIR/LIR/LLVM beserta alias `emit-*`, `build`/`build-native`, dan `doc`. Kasus mencakup trailing EOF, komentar saja, delimiter tanpa isi, komentar dalam body/expression/parameter, nested yang belum selesai, CRLF, tanpa final newline, dan UTF-8 termasuk offset byte pada baris yang sama.
- Setiap mode harus keluar 1, stdout kosong, tepat satu header diagnostic, lokasi dan underline `^^` tepat, tanpa ANSI saat `NO_COLOR=1`, serta tidak membuat berkas atau direktori apa pun selain sumber awal. Build memakai output eksplisit di direktori disposable.
- Fixture positif diuji pada seluruh mode, lalu kedua alias build dijalankan dengan stdout exact `4\n7\n2\n/* not a comment */\n`. Ia memadukan closed/nested comments, adjacency token, `if/else if/else`, `pick`, `scan`/`skip`, `loop`/`stop`, arithmetic/division, calls/implicit return, string/Char, dan array. Closed comment tepat pada EOF tanpa newline juga valid.
- Pada POSIX, `NO_COLOR=1` diuji pula lewat PTY, bukan hanya redirection.

Runner baru benar-benar **gagal pada baseline** karena `trailing/check` keluar 0, dan **lulus pada kandidat**. Perintah reproduksi gate:

```text
CLAW_EXE=<compiler> bash claw/tests/frontend/run_lexer_comment_tests.sh
CLAW_EXE=<clean-release-compiler> bash claw/tests/run_all_tests.sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 CLAW_EXE=<instrumented-compiler> bash claw/tests/run_all_tests.sh
```

Full clean Release, separate compiler ASan/UBSan/LSan, independent build/run review, dan CI Linux/Windows UCRT64 pada head final tetap merupakan gate PR; hasilnya dicatat dalam PR. Fix terbatas ini bukan bukti bahwa v0.1–v0.3 matang seluruhnya atau gate v0.4 selesai.

## Oracle lintas platform

CI Windows awal pada `b6ad86b3d3eb53d988ac1dfff40c7cb5c8f330a3` menemukan mismatch harness, bukan penerimaan compiler: compiler sudah keluar 1 dengan diagnostic/span benar, tetapi runner membandingkan path MSYS `/tmp/...` dengan path native UCRT64 `D:\...`. Runner kini mengonversi expected **full path** melalui `cygpath -w` pada MSYS/Cygwin; tidak menurunkannya menjadi kecocokan basename atau menghapus cek lokasi. Native stdout dibandingkan byte-exact dengan LF pada POSIX dan CRLF pada Windows sesuai text-mode runtime yang sudah ada; tidak ada penghapusan whitespace umum. Semua mode, kasus, span, no-artifact, dan NO_COLOR assertions tetap wajib. Head setelah koreksi harus mengulang review dan seluruh gate final.

Run Windows berikutnya membuktikan 12 mode pertama pada kasus trailing lulus, tetapi jalur `doc` menampilkan full path dengan `/`, berbeda dari `\` project loader. Runner menormalkan **hanya separator path dalam location-line**, kemudian mewajibkan daftar lokasi tepat satu dan full-path/line/column exact. Ini lebih ketat daripada substring match sebelumnya dan tidak menerima lokasi lain atau melewati mode `doc`.
