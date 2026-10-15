/* `let` destructuring — the struct pattern `S { a, b }` / `S { a: x, .. }`
 * and the constructor pattern `P(a, b)` bind each field to a new local of
 * the field's C type (SymTab), from one temp holding the initializer
 * (single evaluation). Named-field access for `pstruct`, declaration-index
 * access for the positional `ctor`; `mut` binds and `_` are allowed. The
 * refutable constructor binds (Some/Ok/Err) are not irrefutable lets —
 * those demand the diverging else (`constructor let without else`
 * refuses without one). */
pub struct S {
    a: u8,
    b: u32,
}
pub struct P {
    x: u8,
    y: u8,
}

pub fn named() -> u32 {
    let s: S = S { a: 7, b: 40 };
    let S { a, b } = s;
    b + (a as u32)
}

pub fn renamed() -> u32 {
    let s: S = S { a: 1, b: 2 };
    let S { a: v, .. } = s;
    v as u32
}

pub fn mutable() -> u32 {
    let s: S = S { a: 5, b: 6 };
    let S { a: mut m, b } = s;
    m = m + 1;
    (m as u32) + b
}

pub fn positional() -> u32 {
    let p: P = P { x: 3, y: 4 };
    let P(m, n) = p;
    (m as u32) + (n as u32)
}

pub fn rest_drop() -> u32 {
    let p: P = P { x: 9, y: 1 };
    let P(_, n) = p;
    n as u32
}
