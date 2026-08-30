/* Value-position if/else with pointer arms: as_ptr on a string literal must
 * not gain a second star, and the tail expression must lower to a return.
 * Regressed 2026-08-28 as `char **` in the generated C and as missing
 * returns in Lexer_at/Lexer_peek (UB the whole lexer inherited). */
pub fn pick(fixed: bool) -> *const u8 {
    let z = if fixed {
        b"[;]\0".as_ptr()
    } else {
        b"[]\0".as_ptr()
    };
    let zl = if fixed { 3 } else { 2 };
    let _ = zl;
    z
}

pub fn at(i: usize) -> u8 {
    if i < 8 {
        7
    } else {
        0
    }
}