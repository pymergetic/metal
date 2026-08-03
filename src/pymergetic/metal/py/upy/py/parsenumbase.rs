//! parsenumbase — numeric base-prefix stripping (upstream `py/parsenumbase.c`).
//!
//! Faithful, complete port: finds the real radix base and strips a leading
//! `0x`/`0o`/`0b` prefix. In base-0 mode, a bare leading `0` (no recognised
//! prefix) sets `*base = 1` as a sentinel so the caller can tell "starts
//! with 0" apart from "starts with 0 but base already fixed" -- matching
//! upstream exactly (the caller then requires the rest to be all zero
//! digits, or raises).

/// Returns the number of bytes to skip (the prefix length) and updates
/// `*base`. `*base == 0` on entry means "figure out the base from the
/// prefix, default to 10".
pub fn parse_num_base(s: &[u8], base: &mut i32) -> usize {
    if s.len() <= 1 {
        if *base == 0 {
            *base = 10;
        }
        return 0;
    }
    let c0 = s[0];
    if c0 != b'0' {
        if *base == 0 {
            *base = 10;
        }
        return 0;
    }
    let c1 = s[1] | 0x20; // lowercase
    let b = *base;
    if c1 == b'x' && (b == 0 || b == 16) {
        *base = 16;
        2
    } else if c1 == b'o' && (b == 0 || b == 8) {
        *base = 8;
        2
    } else if c1 == b'b' && (b == 0 || b == 2) {
        *base = 2;
        2
    } else {
        if b == 0 {
            *base = 1;
        }
        0
    }
}
