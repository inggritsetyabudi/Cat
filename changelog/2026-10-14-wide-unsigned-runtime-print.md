# Wide unsigned runtime printing (2026-10-14)

## Temuan dan perbaikan

Review mereproduksi bahwa `UInt32`, `UInt64`, dan `USize` menggunakan printer bertipe signed sehingga nilai di atas sign bit tampil sebagai bilangan negatif. Runtime kini menyediakan handler `print`/`println` unsigned yang terpisah untuk `UInt16`, `UInt32`, `UInt64`, dan `USize`; `UInt8` tetap pada handler yang sudah ada. Lowering memakai symbol `.u16`, `.u32`, `.u64`, dan `.usize`, dengan `zeroext` konsisten di deklarasi serta callsite untuk `UInt16`. Jalur signed `.i64` tetap digunakan untuk `Int64`.

Fixture native membandingkan output exact untuk sign bit dan nilai maksimum setiap unsigned wide (kedua bentuk `print` dan `println`), sekaligus memastikan `Int64` tetap tercetak signed.

## Validasi

CMake build dengan Clang 18, backend/native suites terfokus, dan full `claw/tests/run_all_tests.sh` lulus di disposable copy. Assertion LLVM mengonfirmasi symbol runtime unsigned dan ABI attributes deklarasi/callsite konsisten; fixture exact-boundary lulus.
