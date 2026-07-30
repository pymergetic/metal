//! Sync ustar header walk — list entries from a memory archive.
//!
//! Async extract (streaming file bodies to a sink across await points) is
//! intentionally not here; call sites that need it should compose a later
//! async helper on top of this sync foreach. No fake await.
#![cfg_attr(target_os = "none", no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_rt as _;


const BLOCK: usize = 512;
const NAME_MAX: usize = 100;

/// Callback per entry. `data`/`data_len` point at the file payload in the
/// archive (empty for directories). Return 0 to continue, non-zero to abort.
pub type pm_metal_util_tar_foreach_fn = Option<
    unsafe extern "C" fn(
        ctx: *mut u8,
        name: *const u8,
        size: u64,
        is_dir: i32,
        data: *const u8,
        data_len: usize,
    ) -> i32
>;

fn parse_octal(p: *const u8, n: usize) -> Option<u64> {
    let mut v = 0u64;
    let mut seen = false;
    unsafe {
        for i in 0..n {
            let c = *p.add(i);
            if c == 0 || c == b' ' {
                if seen {
                    break;
                }
                continue;
            }
            if c < b'0' || c > b'7' {
                return None;
            }
            seen = true;
            v = (v << 3) | (c - b'0') as u64;
        }
    }
    Some(v)
}

fn checksum_ok(hdr: &[u8; BLOCK]) -> bool {
    let mut sum_unsigned = 0u32;
    let mut sum_signed = 0i32;
    for i in 0..BLOCK {
        let b = if (148..156).contains(&i) {
            b' '
        } else {
            hdr[i]
        };
        sum_unsigned += b as u32;
        sum_signed += b as i8 as i32;
    }
    let stored = match parse_octal(hdr[148..156].as_ptr(), 8) {
        Some(v) => v as u32,
        None => return false,
    };
    stored == sum_unsigned || stored == sum_signed as u32
}

fn is_zero_block(hdr: &[u8; BLOCK]) -> bool {
    hdr.iter().all(|&b| b == 0)
}

fn padded(size: u64) -> usize {
    let s = size as usize;
    let rem = s % BLOCK;
    if rem == 0 {
        s
    } else {
        s + (BLOCK - rem)
    }
}

/// Walk a ustar archive in `archive[..len]`, invoking `cb` for each entry.
/// Returns number of entries visited, or -1 on malformed input / callback abort.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_tar_foreach(
    archive: *const u8,
    len: usize,
    cb: pm_metal_util_tar_foreach_fn,
    ctx: *mut u8,
) -> i32 {
    let Some(cb) = cb else {
        return -1;
    };
    if archive.is_null() {
        return -1;
    }
    let mut off = 0usize;
    let mut count = 0i32;
    let mut zero_run = 0u32;

    while off + BLOCK <= len {
        let hdr_ptr = archive.add(off);
        let mut hdr = [0u8; BLOCK];
        for i in 0..BLOCK {
            hdr[i] = *hdr_ptr.add(i);
        }
        off += BLOCK;

        if is_zero_block(&hdr) {
            zero_run += 1;
            if zero_run >= 2 {
                return count;
            }
            continue;
        }
        zero_run = 0;

        if !checksum_ok(&hdr) {
            return -1;
        }
        /* "ustar\0" / "ustar " or all-zero magic (pre-POSIX); reject other. */
        let ustar = hdr[257] == b'u'
            && hdr[258] == b's'
            && hdr[259] == b't'
            && hdr[260] == b'a'
            && hdr[261] == b'r';
        let empty_magic = hdr[257] == 0
            && hdr[258] == 0
            && hdr[259] == 0
            && hdr[260] == 0
            && hdr[261] == 0;
        if !ustar && !empty_magic {
            return -1;
        }

        let size = match parse_octal(hdr[124..136].as_ptr(), 12) {
            Some(v) => v,
            None => return -1,
        };
        let typeflag = hdr[156];
        let mut name_end = 0usize;
        while name_end < NAME_MAX && hdr[name_end] != 0 {
            name_end += 1;
        }
        let name_is_dir = name_end > 0 && hdr[name_end - 1] == b'/';
        let is_dir = if typeflag == b'5' || ((typeflag == 0 || typeflag == b'0') && name_is_dir) {
            1
        } else {
            0
        };

        let mut name = [0u8; NAME_MAX + 1];
        let mut nlen = 0usize;
        while nlen < NAME_MAX && hdr[nlen] != 0 {
            name[nlen] = hdr[nlen];
            nlen += 1;
        }
        name[nlen] = 0;

        let data_off = off;
        let data_len = if is_dir != 0 { 0 } else { size as usize };
        if data_off + data_len > len {
            return -1;
        }
        let data = if data_len == 0 {
            core::ptr::null()
        } else {
            archive.add(data_off)
        };

        let rc = cb(
            ctx,
            name.as_ptr(),
            size,
            is_dir,
            data,
            data_len,
        );
        if rc != 0 {
            return -1;
        }
        count += 1;

        off += padded(size);
        if off > len {
            return -1;
        }
    }

    /* Truncated archive (no dual zero blocks) — still OK if we consumed all. */
    if off == len || zero_run > 0 {
        return count;
    }
    -1
}
