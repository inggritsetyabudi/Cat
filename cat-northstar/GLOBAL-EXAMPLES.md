# Contoh migrasi global G3–G8

Dokumen pendamping [PRD](PRD.md) ini memperlihatkan penggantian sintaks, bukan penerimaan compiler atau pengganti seluruh bab historis. Sumber kewajiban tetap [PRD-CHANGES §2–3](PRD-CHANGES-v0.1-to-v1.0.md#3-perubahan-per-bagian-prd). Arsip `legacy/` tidak diubah dan tidak digunakan sebagai contoh aktif. Fence `cat proposal` diperiksa validator leksikal, tetapi tidak dikompilasi oleh doc-test.

## Config — shape/choice dan identitas

[PRD §1](PRD.md#1-konstruksi-shape-dan-named-arguments) sudah menampilkan `Config.new` yang mengembalikan `Result` serta literal `Config { ... }` dalam modul pendefinisi. Pengganti contoh identitas historis §22 berikut memakai constructor yang sama, bukan menganggap hasil `Result` langsung sebagai `Config`. Kontrak `Equal` dan matriks error identitas tetap bukan bagian pembuktian sintaks ini.

```cat proposal
val a = try Config.new("localhost", 8080) else err { return Fail(err) }
val b = try Config.new("localhost", 8080) else err { return Fail(err) }
val r1 = ref a
val r2 = ref a
val r3 = ref b
val same = r1 is r2
val different = r1 is r3
```

## Buffer dan Circle — modul pendefinisi

Pengganti ekspresi constructor historis §14 mempertahankan field dan argumen posisional. Literal lokal di bawah sengaja tidak diganti menjadi pemanggilan `new` rekursif. `Vec.new()` historis diganti literal `vec[]` dengan tipe dari field `data`. Cuplikan ini tidak mengklaim seluruh metode/kontrak Buffer dan Circle sudah dimigrasikan atau diuji compiler.

```cat proposal
shape Buffer {
    data: Vec[UInt8]
}
implements Buffer {
    share fn new() -> Buffer {
        return Buffer { data: vec[] }
    }
}
val buffer = Buffer.new()

shape Circle {
    x: Float32
    y: Float32
    radius: Float32
}
implements Circle {
    share fn new(x: Float32, y: Float32, r: Float32) -> Circle {
        return Circle { x: x, y: y, radius: r }
    }
}
val circle = Circle.new(0.0, 0.0, 1.0)
```

## Vector2 — pemanggilan dari contoh operator

Pengganti ekspresi historis §15 berikut mengasumsikan `Vector2.new(x, y)` tersedia dari modul tipe. Definisi tipe, implementasi `Add`/`Equal`/`Mul`, dan diagnostik operator tidak dibuktikan oleh cuplikan ini. Di implementasi `add`, hasil lama dengan field bernama diganti `Vector2.new(self.x + other.x, self.y + other.y)`; tidak menambah aturan operator.

```cat proposal
implements Vector2 with Add[Vector2] {
    fn add(self: ref, other: ref Vector2) -> Vector2 {
        return Vector2.new(self.x + other.x, self.y + other.y)
    }
}
val v1 = Vector2.new(1.0, 2.0)
val v2 = Vector2.new(3.0, 4.0)
val v3 = v1 + v2
```

## Node dan Cache — migrasi sintaks, bukan acceptance Arena

Pengganti panggilan historis §11 berikut hanya mendemonstrasikan argumen posisional. `Node.new` dan `Cache.new` diasumsikan disediakan modul pendefinisi. `Maybe[ref Node]` memakai `Some`/`None`; borrowed storage, lifetime, dan rekursi bukan klaim dukungan compiler. Arena tetap eksperimental, di luar gerbang v1; migrasi doubly linked/graph ke `vec` + indeks dan audit safety tetap residual terpisah. `Cache` memakai owned `String` sesuai PRD §12, bukan klaim historis bahwa `Str` adalah owned.

```cat proposal
val n1 = arena.alloc(Node.new(1, None))
val n2 = arena.alloc(Node.new(2, Some(ref n1)))
val n3 = arena.alloc(Node.new(3, Some(ref n2)))
val cache = Cache.new(Anchor.new(String.from("hello")))
```

## Batas bukti

Contoh di sini memperbaiki kekosongan artefak migrasi, bukan membuktikan semua konteks negatif/positif arsip sudah diganti. Validasi locality literal, resolusi constructor, kontrak, ownership, dan seluruh bab memerlukan bukti lanjutan. [Ledger](FEATURE-STATUS.md) mempertahankan sisa per artefak; tidak ada promosi label compiler atau penutupan G2.
