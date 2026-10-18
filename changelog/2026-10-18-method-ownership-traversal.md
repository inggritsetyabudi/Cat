# Pemeriksaan ownership method untuk menutup bypass proyeksi

Review patch proyeksi owned shape menemukan bahwa body `implements` belum dikunjungi oleh ownership checker. Selain itu, method call hanya memperoleh borrow untuk receiver view; receiver owned belum dicatat sebagai transfer kepemilikan. Guard proyeksi saja tidak cukup untuk menutup jalur tersebut melalui method.

Ownership checker kini mengunjungi method pada `implements`, menggunakan `receiverType` untuk parameter `self` dan indeks tersendiri untuk parameter non-self, serta mengonsumsi receiver owned pada method call. Receiver owned diperiksa tepat sekali: mengecek callee terlebih dahulu lalu receiver yang sama lagi dapat salah mengonsumsi argumen milik temporary receiver dua kali. Jalur borrow receiver tetap dipertahankan. Lowering yang ada sudah memetakan receiver melalui `receiverType` dan memakai hasil ownership untuk drop; tidak ada perubahan ABI atau runtime.

Ini menerapkan kontrak ownership yang sama pada method dan fungsi bebas, bukan implementasi partial move shape. Regresi permanen mencakup consuming projected receiver, projection di body/return/tail method serta static constructor, whole-receiver transfer dan cleanup, temporary receiver dengan argumen owned, penggunaan receiver setelah move di body maupun caller, serta pemetaan method dengan beberapa parameter. Borrowed-view `clone`/`as_str` tetap diperiksa sebagai akses yang tidak memindahkan field.

Hasil eksekusi final dicatat dalam [gerbang regresi owned shape](2026-10-18-owned-shape-safety-regressions.md); inspeksi sumber independen tidak menggantikan pengujian native, sanitizer, dan hosted CI.
