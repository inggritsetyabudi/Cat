# Menjelaskan batas argumen inline dan builtin cetak di SPEC

`SPEC.md` kini menjelaskan bahwa nilai `if`/`pick` dapat dipakai langsung sebagai argumen panggilan tanpa binding `val` perantara. Parameter konkret tetap memberikan konteks tipe; parameter dinamis builtin cetak tidak boleh menghapus hasil inference.

Dokumen juga membedakan domain cetak native yang tersedia dari general aggregate formatting. Builtin membutuhkan tepat satu argumen, dan tipe tanpa dukungan runtime harus ditolak pada lokasi argumen sebelum LLVM/native generation. `Never` dapat menghentikan evaluasi sebelum call. Larangan cetak aggregate bukan larangan mengirim aggregate yang didukung ke fungsi user bertipe.

Referensi bukti diarahkan ke fixture komposisi dan matriks penolakan yang masuk suite lengkap. Perubahan ini tidak mengganti PRD, menaikkan versi, mempromosikan milestone, atau menyatakan seluruh kombinasi fitur aman. Validasi final mengikuti clean Release/sanitizer full suite dan CI pada PR implementasi terkait.
