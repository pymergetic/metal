struct Pair<T> {
    a: T,
    b: T,
}

fn sum(p: Pair<u8>) -> u8 {
    p.a + p.b
}

static G: Pair<u8> = Pair { a: 1u8, b: 2u8 };

fn main() {
    let p = Pair { a: 1u8, b: 2u8 };
    let x = sum(p);
    let q: Pair<bool> = Pair { a: true, b: false };
    let y = q.a;
    let g = G.a;
}
