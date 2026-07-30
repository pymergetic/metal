//! FIGlet "small" ASCII art render — rows delivered via a write callback.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_rt as _;

#[path = "_fig_small.rs"]
mod fig_small;

use fig_small::{FigGlyph, FIG_COUNT, FIG_FIRST, FIG_FONT, FIG_H, FIG_MAX_W};

const MAX_W: usize = 160;

/// Callback: one fig row (no trailing NUL). Return 0 to continue, non-zero to abort.
pub type pm_metal_util_ascii_write_fn =
    Option<unsafe extern "C" fn(ctx: *mut u8, s: *const u8, n: usize) -> i32>;

fn glyph(ch: u8) -> &'static FigGlyph {
    let mut c = ch;
    if c < FIG_FIRST || (c as usize) >= (FIG_FIRST as usize) + FIG_COUNT {
        c = b'?';
    }
    &FIG_FONT[(c - FIG_FIRST) as usize]
}

fn glyph_width(g: &FigGlyph) -> usize {
    let mut w = 0usize;
    for r in 0..FIG_H {
        let row = g.row[r].as_bytes();
        let mut c = row.len();
        while c > 0 && row[c - 1] == b' ' {
            c -= 1;
        }
        if c > w {
            w = c;
        }
    }
    w
}

fn text_len(text: *const u8) -> usize {
    if text.is_null() {
        return 0;
    }
    let mut n = 0usize;
    unsafe {
        while *text.add(n) != 0 {
            n += 1;
            if n > 4096 {
                break;
            }
        }
    }
    n
}

/// Worst-case output bytes (incl. NULs between rows) for `text_len` input chars.
#[no_mangle]
pub extern "C" fn pm_metal_util_ascii_bound(text_len: usize) -> usize {
    if text_len == 0 {
        return 1;
    }
    let w = core::cmp::min(text_len * FIG_MAX_W, MAX_W);
    let h = core::cmp::min(FIG_H * (text_len + 1), 32);
    h * (w + 1) + 1
}

/// Render NUL-terminated `text` as FIGlet rows; call `cb` once per non-blank trailing row.
/// Returns 0 on success, -1 on bad args / overflow / callback abort.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_ascii_render(
    text: *const u8,
    cb: pm_metal_util_ascii_write_fn,
    ctx: *mut u8,
) -> i32 {
    let Some(write) = cb else {
        return -1;
    };
    if text.is_null() {
        return -1;
    }

    let mut lines = [[b' '; MAX_W]; FIG_H];
    let mut lens = [0usize; FIG_H];

    let n = text_len(text);
    for i in 0..n {
        let ch = *text.add(i);
        if ch == b'\n' {
            continue;
        }
        let g = glyph(ch);
        let gw = glyph_width(g);
        for r in 0..FIG_H {
            if lens[r] + gw > MAX_W {
                return -1;
            }
            let row = g.row[r].as_bytes();
            for c in 0..gw {
                let b = if c < row.len() { row[c] } else { b' ' };
                lines[r][lens[r] + c] = b;
            }
            lens[r] += gw;
        }
    }

    let mut end = FIG_H as isize - 1;
    while end >= 0 {
        let r = end as usize;
        let mut c = lens[r];
        while c > 0 && lines[r][c - 1] == b' ' {
            c -= 1;
        }
        if c > 0 {
            break;
        }
        end -= 1;
    }

    for r in 0..=end {
        let r = r as usize;
        let mut c = lens[r];
        while c > 0 && lines[r][c - 1] == b' ' {
            c -= 1;
        }
        if write(ctx, lines[r].as_ptr(), c) != 0 {
            return -1;
        }
    }
    0
}

/// Convenience: render a single uppercase block line for short banners without a callback.
/// Writes up to `out_cap-1` bytes + NUL. Returns bytes written excl. NUL, or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_ascii_banner_line(
    text: *const u8,
    out: *mut u8,
    out_cap: usize,
) -> i32 {
    if text.is_null() || out.is_null() || out_cap == 0 {
        return -1;
    }
    struct Buf {
        p: *mut u8,
        cap: usize,
        n: usize,
        rows: usize,
        bad: bool,
    }
    unsafe extern "C" fn append(ctx: *mut u8, s: *const u8, n: usize) -> i32 {
        let b = &mut *(ctx as *mut Buf);
        if b.rows > 0 {
            if b.n + 1 >= b.cap {
                b.bad = true;
                return -1;
            }
            *b.p.add(b.n) = b'\n';
            b.n += 1;
        }
        if b.n + n >= b.cap {
            b.bad = true;
            return -1;
        }
        for i in 0..n {
            *b.p.add(b.n + i) = *s.add(i);
        }
        b.n += n;
        b.rows += 1;
        0
    }
    let mut buf = Buf {
        p: out,
        cap: out_cap,
        n: 0,
        rows: 0,
        bad: false,
    };
    let rc = pm_metal_util_ascii_render(
        text,
        Some(append),
        &mut buf as *mut Buf as *mut u8,
    );
    if rc != 0 || buf.bad || buf.n >= out_cap {
        return -1;
    }
    *out.add(buf.n) = 0;
    buf.n as i32
}
