# Handoff compiler eksplisit untuk runner owned shape

Hosted CI pertama pada commit `023050e70fd00d7cd38bdb5462716bb5afe6f7e1` meluluskan Linux, tetapi Windows UCRT64 gagal tepat saat memasuki runner owned-shape yang baru. Suite native induk sudah memakai `claw/build-ci/claw.exe`; runner anak justru melaporkan path fallback `claw/build/claw` tidak ada. Kegagalan itu adalah pemilihan executable pada batas runner, bukan bukti kegagalan regresi ownership. Penyebab mendasar lingkungan shell MSYS2 tidak disimpulkan hanya dari log tersebut.

Runner induk kini meneruskan path compiler yang telah dipakai sebagai argumen ber-quote. Runner anak memprioritaskan argumen itu, dengan `CLAW_EXE` dan default tetap tersedia untuk pemanggilan standalone. Regresi kontrak runner memasok argumen path invalid yang mengandung spasi meskipun environment menunjuk compiler valid; runner harus menolak path tersebut, tidak diam-diam memilih compiler lain. Seluruh kasus ownership tetap memakai compiler yang sama dengan suite induk.

Tidak ada perubahan checker, ABI, runtime, Arena, atau semantik division dalam perbaikan ini. Readiness tetap ditahan sampai gerbang lokal dan kedua job CI untuk commit terbaru selesai hijau.

Validasi lokal perbaikan lulus: shell syntax; pemanggilan suite dengan `CLAW_EXE` dihapus dari environment dan compiler eksplisit pada path ber-spasi; seluruh 35 fixture termasuk 16 eksekusi sanitizer; serta build disposable Linux Release baru dan `CLAW_REQUIRE_SANITIZERS=1 bash claw/tests/run_all_tests.sh`. Review independen menyetujui kontrak argumen dan pemisahan bukti kegagalan dari dugaan akar masalah MSYS2. Hosted CI tetap diuji ulang pada commit follow-up, tanpa menganggap hasil commit sebelumnya berlaku bagi commit baru.
