/* Destructure inside closure bodies — a closure (as a synthesized
 * value fn) lowers struct/ctor/tuple `let` patterns and refutable
 * let-else binds inside its own body: the destructure rides the same
 * single-evaluation temp and the binds land in the synthesized fn's
 * scope. Nested value closures each destructure in their own body.
 * Boundary: still no captures (`capturing closure as value` refuses);
 * patterns stay flat (nested patterns refuse); the let-else else block
 * must diverge — `return 0` there returns from the CLOSURE. */
struct P {
    x: u8,
    y: u8,
}
struct Tup {
    m: u8,
    n: u8,
}

fn parse(n: i32) -> Result<i32, i32> {
    if n < 0 {
        Err(-n)
    } else {
        Ok(n + 1)
    }
}

pub fn struct_in_body() -> u8 {
    let g: fn(P) -> u8 = |p: P| {
        let P(a, b) = p;
        a + b
    };
    /* 3 + 4 */
    g(P { x: 3, y: 4 })
}

pub fn nested_both() -> u8 {
    let g: fn(P) -> u8 = |p: P| {
        let P(a, b) = p;
        let h: fn(u8) -> u8 = |q: u8| {
            let Tup { m, n } = Tup { m: q, n: 0 };
            m + n
        };
        h(a + b)
    };
    /* h(7) = 7 */
    g(P { x: 3, y: 4 })
}

pub fn tuple_in_body() -> u8 {
    let g: fn(u8) -> u8 = |q: u8| {
        let (m, n) = (q, q + 1);
        m + n
    };
    /* 5 + 6 */
    g(5)
}

pub fn let_else_in_body(n: i32) -> u8 {
    let f: fn(i32) -> u8 = |k: i32| {
        let Ok(v) = parse(k) else {
            return 0;
        };
        (v * 2) as u8
    };
    /* k >= 0 -> (k+1)*2; k < 0 -> 0 (the return leaves the closure) */
    f(n)
}
