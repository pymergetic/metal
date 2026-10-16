/* Nested generics: generic structs composing with each other — the
 * monomorph rows (rsx_gp_<n>) nest as FIELD TYPES of other rows, so a
 * chain like `w.w.w.a` crosses three row layouts before it names a
 * primitive. Also covers multi-param rows (Both<A, B>) with generic args
 * and generic structs crossing fn param/return boundaries. The selfhost
 * corpus pin: a row registered through one path must be FOUND through
 * every other (the SymTab name-ownership bug that once made
 * `w.w.a` fail only when a 2-param row interned earlier). */

struct Pair<T> {
    a: T,
    b: T,
}

struct Wrap<T> {
    w: T,
}

struct Both<A, B> {
    x: A,
    y: B,
}

fn take(p: Pair<u8>) -> u8 {
    p.a + p.b
}

fn give() -> Pair<u8> {
    Pair { a: 1u8, b: 2u8 }
}

fn swap(p: Both<u8, bool>) -> (u8, bool) {
    (p.x, p.y)
}

fn chain2() -> u8 {
    let w: Wrap<Wrap<Pair<u8>>> = Wrap { w: Wrap { w: Pair { a: 1u8, b: 2u8 } } };
    w.w.w.a + w.w.w.b
}

fn main() {
    let p = Pair { a: 1u8, b: 2u8 };
    let w: Wrap<Pair<u8>> = Wrap { w: p };
    let q = w.w.a + w.w.b;
    let b: Both<Pair<u8>, bool> = Both { x: Pair { a: 3u8, b: 4u8 }, y: true };
    let r = b.x.a + b.x.b;
    let t = give();
    let s = take(t);
    let u = take(give());
    let (m, n) = swap(Both { x: 5u8, y: false });
    let mut k = chain2();
    if n {
        k += 1;
    }
    if m > 0 {
        k += m;
    }
    let _ = q + r + s + u + k;
}
