//! LZ4 block format — sync decompress (legacy sequence format, no frame).
//!
//! Compress is not provided here; decompress covers the useful subset for
//! reading packs produced by the host / vendor LZ4 compressor.
#![cfg_attr(target_os = "none", no_std)]
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
