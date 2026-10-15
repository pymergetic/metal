/* Closures as values: a capture-less closure lowers to a synthesized
 * static C fn (rsx_clo_<n>) and the use site carries its designator —
 * a plain fn pointer. The fn-ptr ascription (`: fn(..) -> R`) types the
 * synthesized signature; without one the closure's own `: T` parameter
 * annotations and inferred return (from the body's tail value) do. A
 * nested value closure is a second file-scope fn (its proto flushes with
 * the outer one's). Closures that capture outer locals are refused
 * (`capturing closure as value`) — a synthesized plain C fn has no
 * environment slot. */
pub fn apply(f: fn(u8) -> u8, v: u8) -> u8 {
    f(v)
}

pub fn bump_all() -> u8 {
    let g: fn(u8) -> u8 = |x: u8| x + 1;
    let h = |x: u8, y: u8| x + y;
    let b = |x: u8| {
        x * 2
    };
    let n: u8 = apply(g, 41);
    let m: u8 = h(n, 0);
    b(m)
}

pub fn nested_apply() -> u8 {
    let outer: fn(u8) -> u8 = |x: u8| {
        let inner: fn(u8) -> u8 = |y: u8| y * 2;
        return inner(x);
    };
    outer(21)
}
