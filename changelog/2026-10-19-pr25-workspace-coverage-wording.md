# PR25 — lokasi bukti array lintas panggilan

README integrasi kini menempatkan cakupan array lintas panggilan pada `composition_core.cat`. Fixture `composition_workspace/` mencakup manifest, import alias, shared function melalui modul saudara, choice, if/pick, dan range scan, bukan array lintas panggilan.

Koreksi ini hanya menyelaraskan deskripsi dengan isi fixture. Tidak ada penambahan cakupan, perubahan output yang diharapkan, perubahan runner, atau perubahan semantik compiler.
