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

const RAINBOW_STEPS: u32 = 16;
const RAINBOW_ROWDEG: u32 = 40;

extern "C" {
    fn pm_metal_log(line: *const u8);
}

fn hue_to_rgb(deg: u32) -> (u8, u8, u8) {
    let region = (deg / 60) % 6;
    let rem = (deg % 60) * 255 / 60;
    let rising = rem as u8;
    let falling = (255 - rem) as u8;
    match region {
        0 => (255, rising, 0),
        1 => (falling, 255, 0),
        2 => (0, 255, rising),
        3 => (0, falling, 255),
        4 => (rising, 0, 255),
        _ => (255, 0, falling),
    }
}

fn push_dec(out: &mut [u8], oi: &mut usize, mut v: u8) {
    if *oi >= out.len() {
        return;
    }
    if v >= 100 {
        out[*oi] = b'0' + v / 100;
        *oi += 1;
        v %= 100;
        if *oi >= out.len() {
            return;
        }
        out[*oi] = b'0' + v / 10;
        *oi += 1;
        v %= 10;
    } else if v >= 10 {
        out[*oi] = b'0' + v / 10;
        *oi += 1;
        v %= 10;
    }
    if *oi < out.len() {
        out[*oi] = b'0' + v;
        *oi += 1;
    }
}

/// Colorize one FIGlet row with 24-bit truecolor SGR (same diagonal hue as `_old`).
fn colorize_row(row: &[u8], phase_deg: u32, out: &mut [u8]) -> usize {
    if out.is_empty() {
        return 0;
    }
    let len = row.len();
    let mut oi = 0usize;
    let mut last_step: i32 = -1;
    for (x, &c) in row.iter().enumerate() {
        if c != b' ' {
            let step = (x as u32) * RAINBOW_STEPS / (if len > 0 { len as u32 } else { 1 });
            if step as i32 != last_step {
                let deg = (step * 360 / RAINBOW_STEPS + phase_deg) % 360;
                let (r, g, b) = hue_to_rgb(deg);
                let pref = b"\x1b[38;2;";
                if oi + pref.len() + 12 >= out.len() {
                    break;
                }
                out[oi..oi + pref.len()].copy_from_slice(pref);
                oi += pref.len();
                push_dec(out, &mut oi, r);
                if oi < out.len() {
                    out[oi] = b';';
                    oi += 1;
                }
                push_dec(out, &mut oi, g);
                if oi < out.len() {
                    out[oi] = b';';
                    oi += 1;
                }
                push_dec(out, &mut oi, b);
                if oi < out.len() {
                    out[oi] = b'm';
                    oi += 1;
                }
                last_step = step as i32;
            }
        }
        if oi + 1 < out.len() {
            out[oi] = c;
            oi += 1;
        }
    }
    let reset = b"\x1b[0m";
    if oi + reset.len() < out.len() {
        out[oi..oi + reset.len()].copy_from_slice(reset);
        oi += reset.len();
    }
    if oi < out.len() {
        out[oi] = 0;
    } else if !out.is_empty() {
        out[out.len() - 1] = 0;
        oi = out.len() - 1;
    }
    oi
}

/// FIGlet `text` as rainbow truecolor rows on the log/console path.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_ascii_log_rainbow(text: *const u8) {
    if text.is_null() {
        return;
    }
    struct Ctx {
        row: u32,
    }
    unsafe extern "C" fn on_row(ctx: *mut u8, s: *const u8, n: usize) -> i32 {
        let c = &mut *(ctx as *mut Ctx);
        if s.is_null() {
            return 0;
        }
        let row = core::slice::from_raw_parts(s, n);
        let mut colored = [0u8; 512];
        let _ = colorize_row(row, c.row * RAINBOW_ROWDEG, &mut colored);
        pm_metal_log(colored.as_ptr());
        c.row = c.row.wrapping_add(1);
        0
    }
    let mut ctx = Ctx { row: 0 };
    let _ = pm_metal_util_ascii_render(text, Some(on_row), &mut ctx as *mut Ctx as *mut u8);
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

/* Floor RegMod: publish exports for always-proxy faces (W10.1). */
use core::cell::Cell;
use core::ffi::c_void;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

static FLOOR_ENTRIES: RegModStatic<4, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_util_ascii_bound"),
        RegEntry::new("pm_metal_util_ascii_render"),
        RegEntry::new("pm_metal_util_ascii_banner_line"),
        RegEntry::new("pm_metal_util_ascii_log_rainbow"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_util_ascii_bound as *const c_void,
            pm_metal_util_ascii_render as *const c_void,
            pm_metal_util_ascii_banner_line as *const c_void,
            pm_metal_util_ascii_log_rainbow as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.util.ascii",
    unloadable: false,
    parent: None,
    ctx: core::ptr::null_mut(),
    on_load: None,
    register_symbols: Some(floor_register_symbols),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &FLOOR_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(core::ptr::null()),
    raw_prev: Cell::new(core::ptr::null()),
};

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_ascii_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
