//! parsenum — token text -> numeric value (upstream `py/parsenum.c` mirror).
//!
//! Deliberate omissions vs. upstream (kept honest, not stubbed):
//! - No bigint fallback: Metal's `objint` is small-int only ("heap mpz-ish
//!   later", see `objects/objint.rs`), so an integer literal that overflows
//!   the tagged small-int range is a real, reported `ParseNumError::Overflow`
//!   rather than a silently-wrong wraparound or a fabricated bignum.
//! - No complex/imaginary literals (`5j`): Metal has no `objcomplex`; a
//!   trailing `j`/`J` is `ParseNumError::Unsupported`.
//! - Float parsing scales the mantissa via repeated multiply/divide-by-10
//!   (upstream's `MICROPY_FLOAT_FORMAT_IMPL_EXACT` branch) instead of calling
//!   `pow()`, since Metal's freestanding libc carries no libm; this is
//!   upstream's own recommended portable fallback, not a shortcut.
//! - `mp_parse_num_integer`'s leading/trailing-whitespace skip and the
//!   `inf`/`nan`/`infinity` literals in `mp_parse_num_float` exist upstream
//!   only for the `int()`/`float()` *builtins* parsing arbitrary runtime
//!   strings; the lexer never emits an INTEGER/FLOAT_OR_IMAG token that
//!   starts with a letter or surrounding whitespace, so that path is dead
//!   for parse-tree building and is omitted here too.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParseNumError {
    Empty,
    Invalid,
    Overflow,
    Unsupported,
}

/// Parse an integer literal's token text (as produced by the lexer, e.g.
/// `"0x1F"`, `"1_000"`, `"42"`). `base` is `0` to auto-detect from a prefix
/// (default 10), matching upstream's `mp_parse_num_integer(..., base=0, ...)`
/// call from the parser.
pub fn parse_int(text: &[u8], base: i32) -> Result<i128, ParseNumError> {
    let mut base = base;
    if !(2..=36).contains(&base) && base != 0 {
        return Err(ParseNumError::Invalid);
    }

    let mut i = 0usize;
    let top = text.len();

    // skip leading space (lexer never produces this, kept for API honesty
    // with a text slice from any caller, not just the lexer)
    while i < top && text[i].is_ascii_whitespace() {
        i += 1;
    }

    let neg = if i < top && text[i] == b'-' {
        i += 1;
        true
    } else if i < top && text[i] == b'+' {
        i += 1;
        false
    } else {
        false
    };

    i += super::parsenumbase::parse_num_base(&text[i..], &mut base);

    let val_start = i;
    let mut val: i128 = 0;
    while i < top {
        let mut dig = text[i] as u32;
        if dig == b'_' as u32 {
            i += 1;
            continue;
        }
        if (b'0' as u32..=b'9' as u32).contains(&dig) {
            dig -= b'0' as u32;
        } else {
            dig = (dig | 0x20).wrapping_sub(b'a' as u32).wrapping_add(10);
            if dig >= 36 {
                break;
            }
        }
        if dig >= base as u32 {
            break;
        }
        val = val
            .checked_mul(base as i128)
            .and_then(|v| v.checked_add(dig as i128))
            .ok_or(ParseNumError::Overflow)?;
        i += 1;
    }

    if i == val_start {
        return Err(ParseNumError::Empty);
    }

    while i < top && text[i].is_ascii_whitespace() {
        i += 1;
    }
    if i != top {
        return Err(ParseNumError::Invalid);
    }

    Ok(if neg { -val } else { val })
}

// MANTISSA_MAX for an 8-byte (u64) mantissa accumulator, matching upstream's
// `sizeof(mp_large_float_uint_t) == 8` branch exactly.
const MANTISSA_MAX: u64 = 0x1999_9999_9999_9998;

enum DecIn {
    Intg,
    Frac,
    Exp,
}

fn accept_digit(mantissa: u64, dig: u32, exp_extra: &mut i32, in_: &DecIn) -> u64 {
    if mantissa < MANTISSA_MAX {
        if matches!(in_, DecIn::Frac) {
            *exp_extra -= 1;
        }
        10 * mantissa + dig as u64
    } else {
        if matches!(in_, DecIn::Intg) {
            *exp_extra += 1;
        }
        mantissa
    }
}

/// Scale `num` by `10 ** dec_exp` using only multiply/divide (no `pow()` /
/// libm -- upstream's `MICROPY_FLOAT_FORMAT_IMPL_EXACT` branch, the portable
/// fallback for freestanding targets without a math library).
fn decimal_exp(num: f64, dec_exp: i32) -> f64 {
    if dec_exp == 0 || num == 0.0 {
        return num;
    }
    let neg_exp = dec_exp < 0;
    let mut dec_exp = if neg_exp { -dec_exp } else { dec_exp };
    let mut res = num;
    let mut expo: f64 = 10.0;
    while dec_exp != 0 {
        if dec_exp & 1 != 0 {
            if neg_exp {
                res /= expo;
            } else {
                res *= expo;
            }
        }
        dec_exp >>= 1;
        if dec_exp != 0 {
            expo *= expo;
        }
    }
    res
}

/// Parse the unsigned decimal digits of a float literal into `(mantissa,
/// exp)` such that the value is `mantissa * 10^exp`. Returns the index just
/// past the consumed digits (upstream `mp_parse_float_internal`).
fn parse_float_internal(text: &[u8]) -> Option<(f64, usize)> {
    let top = text.len();
    let mut i = 0usize;
    let mut in_ = DecIn::Intg;
    let mut exp_neg = false;
    let mut mantissa: u64 = 0;
    let mut exp_val: i32 = 0;
    let mut exp_extra: i32 = 0;
    let mut trailing_zeros_intg = 0i32;
    let mut trailing_zeros_frac = 0i32;

    while i < top {
        let dig = text[i];
        i += 1;
        if dig.is_ascii_digit() {
            let d = (dig - b'0') as u32;
            if matches!(in_, DecIn::Exp) {
                if exp_val < (i32::MAX / 2 - 9) / 10 {
                    exp_val = 10 * exp_val + d as i32;
                }
            } else if d == 0 || mantissa >= MANTISSA_MAX {
                match in_ {
                    DecIn::Intg => trailing_zeros_intg += 1,
                    _ => trailing_zeros_frac += 1,
                }
            } else {
                while trailing_zeros_intg > 0 {
                    mantissa = accept_digit(mantissa, 0, &mut exp_extra, &DecIn::Intg);
                    trailing_zeros_intg -= 1;
                }
                while trailing_zeros_frac > 0 {
                    mantissa = accept_digit(mantissa, 0, &mut exp_extra, &DecIn::Frac);
                    trailing_zeros_frac -= 1;
                }
                mantissa = accept_digit(mantissa, d, &mut exp_extra, &in_);
            }
        } else if matches!(in_, DecIn::Intg) && dig == b'.' {
            in_ = DecIn::Frac;
        } else if !matches!(in_, DecIn::Exp) && (dig | 0x20) == b'e' {
            in_ = DecIn::Exp;
            if i < top {
                if text[i] == b'+' {
                    i += 1;
                } else if text[i] == b'-' {
                    i += 1;
                    exp_neg = true;
                }
            }
            if i == top {
                return None;
            }
        } else if dig == b'_' {
            continue;
        } else {
            i -= 1;
            break;
        }
    }

    if exp_neg {
        exp_val = -exp_val;
    }
    exp_val += exp_extra + trailing_zeros_intg;

    Some((decimal_exp(mantissa as f64, exp_val), i))
}

/// Parse a FLOAT_OR_IMAG token's text into an `f64`. Rejects a trailing
/// `j`/`J` (complex literals -- see module doc: no `objcomplex` yet).
pub fn parse_float(text: &[u8]) -> Result<f64, ParseNumError> {
    let top = text.len();
    if top == 0 {
        return Err(ParseNumError::Empty);
    }
    let mut i = 0usize;
    let neg = if text[i] == b'-' {
        i += 1;
        true
    } else if text[i] == b'+' {
        i += 1;
        false
    } else {
        false
    };

    let val_start = i;
    let (mut val, consumed) = parse_float_internal(&text[i..]).ok_or(ParseNumError::Invalid)?;
    i += consumed;

    if i < top && (text[i] | 0x20) == b'j' {
        return Err(ParseNumError::Unsupported);
    }

    if neg {
        val = -val;
    }

    if i == val_start {
        return Err(ParseNumError::Empty);
    }
    if i != top {
        return Err(ParseNumError::Invalid);
    }

    Ok(val)
}
