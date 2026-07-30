//! Sync ustar walk + write helpers (memory archives / `.mtar` packs).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
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

/// Offset-aware callback (header_off / payload_off in archive).
pub type pm_metal_util_tar_foreach_ex_fn = Option<
    unsafe extern "C" fn(
        ctx: *mut u8,
        name: *const u8,
        size: u64,
        is_dir: i32,
        header_off: u64,
        payload_off: u64,
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

unsafe fn walk(
    archive: *const u8,
    len: usize,
    mut emit: impl FnMut(*const u8, u64, i32, u64, u64, *const u8, usize) -> i32,
) -> i32 {
    if archive.is_null() {
        return -1;
    }
    let mut off = 0usize;
    let mut count = 0i32;
    let mut zero_run = 0u32;

    while off + BLOCK <= len {
        let header_off = off as u64;
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

        let rc = emit(
            name.as_ptr(),
            size,
            is_dir,
            header_off,
            data_off as u64,
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

    if off == len || zero_run > 0 {
        return count;
    }
    -1
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
    walk(archive, len, |name, size, is_dir, _ho, _po, data, data_len| {
        cb(ctx, name, size, is_dir, data, data_len)
    })
}

/// Offset-aware ustar walk (header_off / payload_off for seekable packs).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_tar_foreach_ex(
    archive: *const u8,
    len: usize,
    cb: pm_metal_util_tar_foreach_ex_fn,
    ctx: *mut u8,
) -> i32 {
    let Some(cb) = cb else {
        return -1;
    };
    walk(
        archive,
        len,
        |name, size, is_dir, header_off, payload_off, data, data_len| {
            cb(
                ctx,
                name,
                size,
                is_dir,
                header_off,
                payload_off,
                data,
                data_len,
            )
        },
    )
}

fn put_octal(dst: &mut [u8], mut v: u64, width: usize) {
    for i in (0..width).rev() {
        if i == width - 1 {
            dst[i] = 0;
            continue;
        }
        dst[i] = b'0' + (v & 7) as u8;
        v >>= 3;
    }
}

fn fill_checksum(hdr: &mut [u8; BLOCK]) {
    for i in 148..156 {
        hdr[i] = b' ';
    }
    let mut sum = 0u32;
    for b in hdr.iter() {
        sum += *b as u32;
    }
    put_octal(&mut hdr[148..156], sum as u64, 8);
    hdr[155] = b' ';
}

/// Write one ustar header into `out` (must be >= 512). `typeflag`: b'0' file, b'5' dir.
/// Returns 512 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_tar_write_header(
    out: *mut u8,
    out_cap: usize,
    name: *const u8,
    size: u64,
    typeflag: u8,
) -> i32 {
    if out.is_null() || name.is_null() || out_cap < BLOCK {
        return -1;
    }
    let mut hdr = [0u8; BLOCK];
    let mut nlen = 0usize;
    while nlen < NAME_MAX && *name.add(nlen) != 0 {
        hdr[nlen] = *name.add(nlen);
        nlen += 1;
    }
    if nlen == 0 || nlen >= NAME_MAX {
        return -1;
    }
    put_octal(&mut hdr[100..108], 0o644, 8);
    put_octal(&mut hdr[108..116], 0, 8);
    put_octal(&mut hdr[116..124], 0, 8);
    put_octal(&mut hdr[124..136], size, 12);
    put_octal(&mut hdr[136..148], 0, 12);
    hdr[156] = typeflag;
    hdr[257] = b'u';
    hdr[258] = b's';
    hdr[259] = b't';
    hdr[260] = b'a';
    hdr[261] = b'r';
    hdr[262] = 0;
    hdr[263] = b'0';
    hdr[264] = b'0';
    fill_checksum(&mut hdr);
    for i in 0..BLOCK {
        *out.add(i) = hdr[i];
    }
    BLOCK as i32
}

/// Bytes of zero padding to reach the next 512-boundary after `size`.
#[no_mangle]
pub extern "C" fn pm_metal_util_tar_pad_len(size: u64) -> usize {
    let rem = (size as usize) % BLOCK;
    if rem == 0 {
        0
    } else {
        BLOCK - rem
    }
}

/// Write two 512-byte zero blocks (ustar end). Returns 1024 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_tar_write_end(out: *mut u8, out_cap: usize) -> i32 {
    if out.is_null() || out_cap < BLOCK * 2 {
        return -1;
    }
    for i in 0..(BLOCK * 2) {
        *out.add(i) = 0;
    }
    (BLOCK * 2) as i32
}
