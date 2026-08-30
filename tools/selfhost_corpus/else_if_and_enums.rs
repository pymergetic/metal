/* else-if chains of depth >= 3: the third condition used to be dropped
 * entirely, so Lexer_lex_punct assigned kind=RANGE unconditionally and the
 * self-compiler mislexed `{`. */
pub fn chain(a: bool, b: bool, c: bool) -> i32 {
    if a {
        1
    } else if b {
        2
    } else if c {
        3
    } else {
        4
    }
}

/* discriminant parsing plus enum path typing after two enums and a const. */
#[repr(C)]
pub enum Kind {
    P = 0,
    Q = 1,
}

pub const KINDS: u32 = 2;

#[repr(C)]
pub enum Other {
    U = 0,
    V = 1,
}

pub fn discriminate(a: bool, b: bool, c: bool) -> u32 {
    let mut kind = Kind::P;
    if chain(a, b, c) == 2 {
        kind = Kind::Q;
    }
    if kind == Kind::Q {
        KINDS
    } else {
        0
    }
}