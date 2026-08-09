//! LZ4 block format — sync compress/decompress (no frame).
//!
//! Zero crates.io / no vendored lib: greedy hash compressor compatible with
//! [`pm_metal_util_lz4_decompress_safe`].
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_rt as _;

/// Upper bound on compressed size for a `src_len`-byte input (LZ4 formula).
/// Returns 0 if `src_len` is absurdly large for `i32`-sized APIs.
#[no_mangle]
pub extern "C" fn pm_metal_util_lz4_compress_bound(src_len: usize) -> usize {
    if src_len > (i32::MAX as usize) {
        return 0;
    }
    src_len + (src_len / 255) + 16
}

fn write_len_extra(dst: *mut u8, mut di: usize, dst_cap: usize, mut n: usize) -> Option<usize> {
    while n >= 255 {
        if di >= dst_cap {
            return None;
        }
        unsafe {
            *dst.add(di) = 255;
        }
        di += 1;
        n -= 255;
    }
    if di >= dst_cap {
        return None;
    }
    unsafe {
        *dst.add(di) = n as u8;
    }
    Some(di + 1)
}

/// Compress `src` into a raw LZ4 block in `dst`.
/// Returns compressed length, or -1 on error / short buffer.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_lz4_compress(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
) -> i32 {
    if src.is_null() || dst.is_null() {
        return -1;
    }
    if src_len == 0 {
        return 0;
    }
    if src_len > (i32::MAX as usize) || dst_cap > (i32::MAX as usize) {
        return -1;
    }

    /* Greedy LZ4: 4-byte hash, min match 4. */
    const HASH_LOG: usize = 12;
    const HASH_SIZE: usize = 1 << HASH_LOG;
    let mut hash_table = [0u32; HASH_SIZE];

    let mut si = 0usize;
    let mut di = 0usize;
    let mut anchor = 0usize;

    let hash4 = |p: *const u8| -> usize {
        let v = u32::from_le_bytes([*p, *p.add(1), *p.add(2), *p.add(3)]);
        ((v.wrapping_mul(2654435761)) >> (32 - HASH_LOG)) as usize
    };

    while si + 4 < src_len {
        let h = hash4(src.add(si));
        let match_pos = hash_table[h] as usize;
        hash_table[h] = si as u32;

        let mut matched = false;
        if match_pos < si && si - match_pos < 65536 {
            let mut mlen = 0usize;
            while si + mlen < src_len
                && match_pos + mlen < si
                && *src.add(si + mlen) == *src.add(match_pos + mlen)
            {
                mlen += 1;
            }
            if mlen >= 4 {
                let lit_len = si - anchor;
                let token_lit = lit_len;
                let token_match = mlen - 4;
                let mut tlit = token_lit;
                let mut tmatch = token_match;
                if tlit > 15 {
                    tlit = 15;
                }
                if tmatch > 15 {
                    tmatch = 15;
                }
                if di >= dst_cap {
                    return -1;
                }
                *dst.add(di) = ((tlit as u8) << 4) | (tmatch as u8);
                di += 1;
                if token_lit >= 15 {
                    di = match write_len_extra(dst, di, dst_cap, token_lit - 15) {
                        Some(x) => x,
                        None => return -1,
                    };
                }
                if di + lit_len > dst_cap {
                    return -1;
                }
                for i in 0..lit_len {
                    *dst.add(di + i) = *src.add(anchor + i);
                }
                di += lit_len;
                let offset = (si - match_pos) as u16;
                if di + 2 > dst_cap {
                    return -1;
                }
                *dst.add(di) = (offset & 0xff) as u8;
                *dst.add(di + 1) = (offset >> 8) as u8;
                di += 2;
                if token_match >= 15 {
                    di = match write_len_extra(dst, di, dst_cap, token_match - 15) {
                        Some(x) => x,
                        None => return -1,
                    };
                }
                si += mlen;
                anchor = si;
                matched = true;
            }
        }
        if !matched {
            si += 1;
        }
    }

    /* Last literals */
    let lit_len = src_len - anchor;
    let mut tlit = lit_len;
    if tlit > 15 {
        tlit = 15;
    }
    if di >= dst_cap {
        return -1;
    }
    *dst.add(di) = (tlit as u8) << 4;
    di += 1;
    if lit_len >= 15 {
        di = match write_len_extra(dst, di, dst_cap, lit_len - 15) {
            Some(x) => x,
            None => return -1,
        };
    }
    if di + lit_len > dst_cap {
        return -1;
    }
    for i in 0..lit_len {
        *dst.add(di + i) = *src.add(anchor + i);
    }
    di += lit_len;
    di as i32
}

/// Decompress a raw LZ4 block into `dst`.
/// On success returns 0 and writes the decompressed byte count to `out_len`.
/// On malformation / overflow returns -1 (`out_len` unchanged).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_lz4_decompress_safe(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
    out_len: *mut usize,
) -> i32 {
    if src.is_null() || dst.is_null() || out_len.is_null() {
        return -1;
    }
    if src_len == 0 {
        *out_len = 0;
        return 0;
    }

    let mut si = 0usize;
    let mut di = 0usize;

    loop {
        if si >= src_len {
            return -1;
        }
        let token = *src.add(si);
        si += 1;

        let mut lit_len = (token >> 4) as usize;
        if lit_len == 15 {
            loop {
                if si >= src_len {
                    return -1;
                }
                let b = *src.add(si);
                si += 1;
                lit_len += b as usize;
                if b != 255 {
                    break;
                }
            }
        }

        if lit_len > 0 {
            if si + lit_len > src_len || di + lit_len > dst_cap {
                return -1;
            }
            for i in 0..lit_len {
                *dst.add(di + i) = *src.add(si + i);
            }
            si += lit_len;
            di += lit_len;
        }

        /* Last sequence: no match copy after the literals. */
        if si >= src_len {
            *out_len = di;
            return 0;
        }
        if si + 2 > src_len {
            return -1;
        }
        let offset = (*src.add(si) as usize) | ((*src.add(si + 1) as usize) << 8);
        si += 2;
        if offset == 0 || offset > di {
            return -1;
        }

        let mut match_len = (token & 0x0f) as usize;
        if match_len == 15 {
            loop {
                if si >= src_len {
                    return -1;
                }
                let b = *src.add(si);
                si += 1;
                match_len += b as usize;
                if b != 255 {
                    break;
                }
            }
        }
        match_len += 4;

        if di + match_len > dst_cap {
            return -1;
        }
        let mut mpos = di - offset;
        for _ in 0..match_len {
            *dst.add(di) = *dst.add(mpos);
            di += 1;
            mpos += 1;
        }
    }
}

use core::ffi::c_void;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod lz4 = "pymergetic.metal.util.lz4";
    exports: [compress, decompress_safe, compress_bound];
}

extern "C" fn lz4_register_symbols(_ctx: *mut c_void) -> i32 {
    lz4::compress.publish(pm_metal_util_lz4_compress as *const c_void);
    lz4::decompress_safe.publish(pm_metal_util_lz4_decompress_safe as *const c_void);
    lz4::compress_bound.publish(pm_metal_util_lz4_compress_bound as *const c_void);
    0
}

static LZ4_MOD: RegMod = RegMod::from_static(
    lz4::NAME,
    &lz4::STORAGE.exports,
    &lz4::STORAGE.imports,
    Some(lz4_register_symbols),
);

/// Load this module into the kernel RegMod ring (idempotent).
#[no_mangle]
pub extern "C" fn pm_metal_util_lz4_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(lz4::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&LZ4_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_util_lz4_reg_load()
}
