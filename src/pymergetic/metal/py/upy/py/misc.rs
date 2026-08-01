//! misc — small integer / byte aliases and helpers (upstream `misc.h` face).

pub type MpUint = usize;
pub type MpInt = isize;
pub type Byte = u8;

#[inline]
pub const fn min_usize(a: usize, b: usize) -> usize {
    if a < b {
        a
    } else {
        b
    }
}

#[inline]
pub const fn max_usize(a: usize, b: usize) -> usize {
    if a > b {
        a
    } else {
        b
    }
}

/// Align `v` up to `align` (power of two).
#[inline]
pub const fn align_up(v: usize, align: usize) -> usize {
    debug_assert!(align.is_power_of_two() || align == 0);
    if align <= 1 {
        return v;
    }
    (v + align - 1) & !(align - 1)
}
