# Win64 ABI untuk parameter `Span` by-value (2026-10-16)

- Pada Win64, aggregate C berukuran 16 byte seperti `claw_slice { ptr, i64 }` diteruskan sebagai pointer ke salinan argumen. LLVM sebelumnya menurunkan borrowed `Span[Int32]` sebagai aggregate langsung, sehingga callee C@ membaca pointer salinan sebagai pointer elemen dan menerima panjang dari register yang tidak sesuai.
- Parameter borrowed `Span` kini memakai pointer ABI pada target Windows, konsisten antara definisi callee dan direct-call. SysV x64 tetap memakai aggregate langsung. Jalur return/hidden-return tidak diubah karena reproducer mengisolasi mismatch parameter.
- Regresi native mempertahankan output tepat `9 / 42 / 42`, mencakup pemanggilan C ke wrapper C@ by-value, forwarding C@ ke C@, mutation/readback storage asal, serta assertion signature LLVM untuk Win64 dan SysV.
