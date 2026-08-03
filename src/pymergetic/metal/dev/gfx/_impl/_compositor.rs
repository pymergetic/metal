//! Shadow compositor — surfaces, fill/text, present via scanout.

use core::ptr;
use core::sync::atomic::{AtomicU32, Ordering};

use crate::font;
use crate::harvest;
use crate::scanout::{self, Bind};
#[cfg(pm_gfx_ui)]
use crate::ui;

pub type Color = u32;

pub const fn rgb(r: u8, g: u8, b: u8) -> Color {
    (b as u32) | ((g as u32) << 8) | ((r as u32) << 16) | (0xffu32 << 24)
}

struct Surface {
    pixels: *mut u32,
    width: u32,
    height: u32,
    pitch: u32,
}

static mut FB: *mut u32 = ptr::null_mut();
static mut FB_PPSL: u32 = 0;
static mut MODE_W: u32 = 0;
static mut MODE_H: u32 = 0;
static mut GOP: *mut core::ffi::c_void = ptr::null_mut();
static mut OWNED: i32 = 1;
static mut HARVESTED: i32 = 0;
static mut SURF: Surface = Surface {
    pixels: ptr::null_mut(),
    width: 0,
    height: 0,
    pitch: 0,
};
static mut SURF_HEAP: *mut u8 = ptr::null_mut();
static mut READY: i32 = 0;
static PRESENT_BUSY: AtomicU32 = AtomicU32::new(0);

extern "C" {
    fn pm_metal_mem_memalign(align: usize, size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
}

pub unsafe fn harvest() -> i32 {
    if HARVESTED != 0 {
        return 0;
    }
    let mut fb: *mut u32 = ptr::null_mut();
    let mut w = 0u32;
    let mut h = 0u32;
    let mut ppsl = 0u32;
    let mut gop: *mut core::ffi::c_void = ptr::null_mut();
    let mut owned = 1i32;
    if harvest::harvest_any(&mut fb, &mut w, &mut h, &mut ppsl, &mut gop, &mut owned) != 0 {
        return -1;
    }
    if w < 320 || h < 200 || fb.is_null() {
        return -1;
    }
    FB = fb;
    MODE_W = w;
    MODE_H = h;
    FB_PPSL = if ppsl != 0 { ppsl } else { w };
    GOP = gop;
    OWNED = owned;
    HARVESTED = 1;
    0
}

pub unsafe fn harvested() -> i32 {
    if HARVESTED != 0 {
        1
    } else {
        0
    }
}

fn output_init() {
    unsafe {
        let b = Bind {
            shadow: SURF.pixels,
            shadow_w: SURF.width,
            shadow_h: SURF.height,
            shadow_pitch: SURF.pitch,
            fb: FB,
            fb_ppsl: FB_PPSL,
            mode_w: MODE_W,
            mode_h: MODE_H,
            gop: GOP,
            owned: OWNED,
        };
        let _ = scanout::bind(&b);
    }
}

pub unsafe fn init() -> i32 {
    if READY != 0 {
        return 0;
    }
    if HARVESTED == 0 && harvest() != 0 {
        return -1;
    }
    let w = MODE_W;
    let h = MODE_H;
    let pitch = w;
    let bytes = (pitch as usize) * (h as usize) * 4;
    let heap = pm_metal_mem_memalign(4096, bytes);
    if heap.is_null() {
        return -1;
    }
    SURF_HEAP = heap;
    SURF.pixels = heap as *mut u32;
    ptr::write_bytes(SURF.pixels as *mut u8, 0, bytes);
    SURF.width = w;
    SURF.height = h;
    SURF.pitch = pitch;
    READY = 1;
    output_init();
    clear(rgb(0x4a, 0x4a, 0x4a));
    0
}

pub unsafe fn fini() {
    scanout::fini();
    if !SURF_HEAP.is_null() {
        pm_metal_mem_free(SURF_HEAP);
        SURF_HEAP = ptr::null_mut();
    }
    SURF.pixels = ptr::null_mut();
    READY = 0;
    HARVESTED = 0;
    GOP = ptr::null_mut();
    OWNED = 1;
}

/// Pre-ExitBootServices present via `gop_blt` when GOP was stashed.
/// Resets compositor afterward so post-EBS `detect` can rebind owned backends.
pub unsafe fn efi_pre_ebs() -> i32 {
    fini();
    let mut fb: *mut u32 = ptr::null_mut();
    let mut w = 0u32;
    let mut h = 0u32;
    let mut ppsl = 0u32;
    let mut gop: *mut core::ffi::c_void = ptr::null_mut();
    if harvest::harvest_efi_gop(&mut fb, &mut w, &mut h, &mut ppsl, &mut gop) != 0 {
        return 0;
    }
    if gop.is_null() || fb.is_null() || w < 320 || h < 200 {
        return 0;
    }
    FB = fb;
    MODE_W = w;
    MODE_H = h;
    FB_PPSL = if ppsl != 0 { ppsl } else { w };
    GOP = gop;
    OWNED = 0;
    HARVESTED = 1;
    if init() != 0 {
        fini();
        return 0; /* soft-skip — post-EBS bochs/lfb still available */
    }
    clear(rgb(0x20, 0x28, 0x40));
    fill_rect(32, 32, 200, 48, rgb(0x3a, 0x7a, 0xd0));
    draw_text(
        48,
        48,
        b"metal gfx\0".as_ptr(),
        rgb(0xff, 0xff, 0xff),
        rgb(0, 0, 0),
        1,
    );
    if present() != 0 {
        fini();
        return 0;
    }
    fini();
    0
}

pub unsafe fn ready() -> i32 {
    if READY != 0 {
        1
    } else {
        0
    }
}

pub unsafe fn width() -> i32 {
    if READY != 0 {
        SURF.width as i32
    } else {
        0
    }
}

pub unsafe fn height() -> i32 {
    if READY != 0 {
        SURF.height as i32
    } else {
        0
    }
}

pub unsafe fn clear(color: Color) {
    if READY == 0 || SURF.pixels.is_null() {
        return;
    }
    let n = (SURF.pitch as usize) * (SURF.height as usize);
    for i in 0..n {
        *SURF.pixels.add(i) = color;
    }
}

pub unsafe fn fill_rect(x: i32, y: i32, w: i32, h: i32, color: Color) {
    if READY == 0 || SURF.pixels.is_null() || w <= 0 || h <= 0 {
        return;
    }
    let mut x0 = x;
    let mut y0 = y;
    let mut x1 = x + w;
    let mut y1 = y + h;
    if x0 < 0 {
        x0 = 0;
    }
    if y0 < 0 {
        y0 = 0;
    }
    if x1 > SURF.width as i32 {
        x1 = SURF.width as i32;
    }
    if y1 > SURF.height as i32 {
        y1 = SURF.height as i32;
    }
    for yy in y0..y1 {
        let row = SURF.pixels.add((yy as u32 * SURF.pitch) as usize);
        for xx in x0..x1 {
            *row.add(xx as usize) = color;
        }
    }
}

pub unsafe fn draw_text(x: i32, y: i32, text: *const u8, fg: Color, bg: Color, transparent_bg: i32) {
    if READY == 0 || SURF.pixels.is_null() || text.is_null() {
        return;
    }
    let mut cx = x;
    let mut p = text;
    while *p != 0 {
        let ch = *p;
        p = p.add(1);
        if ch == b'\n' {
            cx = x;
            continue;
        }
        for row in 0..font::FONT_H as usize {
            let bits = font::glyph_row(ch, row);
            let py = y + row as i32;
            if py < 0 || (py as u32) >= SURF.height {
                continue;
            }
            for col in 0..font::FONT_W as usize {
                let px = cx + col as i32;
                if px < 0 || (px as u32) >= SURF.width {
                    continue;
                }
                let on = (bits >> (7 - col)) & 1;
                if on != 0 {
                    *SURF.pixels.add((py as u32 * SURF.pitch + px as u32) as usize) = fg;
                } else if transparent_bg == 0 {
                    *SURF.pixels.add((py as u32 * SURF.pitch + px as u32) as usize) = bg;
                }
            }
        }
        cx += font::FONT_W as i32;
    }
}

pub unsafe fn present() -> i32 {
    if READY == 0 {
        return -1;
    }
    if PRESENT_BUSY
        .compare_exchange(0, 1, Ordering::Acquire, Ordering::Relaxed)
        .is_err()
    {
        return -1;
    }
    let rc = scanout::present_rect(0, 0, SURF.width as i32, SURF.height as i32);
    PRESENT_BUSY.store(0, Ordering::Release);
    rc
}

/// Nearest-neighbor (integer-scale fast path) blit into the shadow surface.
/// Does not present — caller fences with present / present_async.
pub unsafe fn blit_bgra(
    dx: i32,
    dy: i32,
    dw: i32,
    dh: i32,
    pixels: *const u8,
    src_w: i32,
    src_h: i32,
    src_pitch: i32,
) -> i32 {
    if READY == 0 || SURF.pixels.is_null() || pixels.is_null() {
        return -1;
    }
    if src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4 || dw <= 0 || dh <= 0 {
        return -1;
    }
    let mut dx = dx;
    let mut dy = dy;
    let mut dw = dw;
    let mut dh = dh;
    if dx < 0 {
        dw += dx;
        dx = 0;
    }
    if dy < 0 {
        dh += dy;
        dy = 0;
    }
    if dw <= 0 || dh <= 0 {
        return 0;
    }
    if dx >= SURF.width as i32 || dy >= SURF.height as i32 {
        return 0;
    }
    if dx + dw > SURF.width as i32 {
        dw = SURF.width as i32 - dx;
    }
    if dy + dh > SURF.height as i32 {
        dh = SURF.height as i32 - dy;
    }
    if dw <= 0 || dh <= 0 {
        return 0;
    }

    /* Integer scale fast path. */
    if (dw % src_w) == 0 && (dh % src_h) == 0 && (dw / src_w) == (dh / src_h) {
        let scale = dw / src_w;
        if scale == 1 {
            let mut y = 0i32;
            while y < src_h {
                let srow = pixels.add((y as usize) * (src_pitch as usize)) as *const u32;
                let drow = SURF
                    .pixels
                    .add(((dy + y) as u32 * SURF.pitch + dx as u32) as usize);
                ptr::copy_nonoverlapping(srow, drow, src_w as usize);
                y += 1;
            }
            return 0;
        }
        if scale > 1 {
            let mut y = 0i32;
            while y < src_h {
                let srow = pixels.add((y as usize) * (src_pitch as usize)) as *const u32;
                let mut ry = 0i32;
                while ry < scale {
                    let drow = SURF.pixels.add(
                        ((dy + y * scale + ry) as u32 * SURF.pitch + dx as u32) as usize,
                    );
                    let mut out = 0i32;
                    let mut sx = 0i32;
                    while sx < src_w {
                        let px = *srow.add(sx as usize);
                        let mut rx = 0i32;
                        while rx < scale {
                            *drow.add(out as usize) = px;
                            out += 1;
                            rx += 1;
                        }
                        sx += 1;
                    }
                    ry += 1;
                }
                y += 1;
            }
            return 0;
        }
    }

    /* Stretch nearest (per-dest-pixel). */
    let mut y = 0i32;
    while y < dh {
        let sy = ((y as i64) * (src_h as i64)) / (dh as i64);
        let srow = pixels.add((sy as usize) * (src_pitch as usize)) as *const u32;
        let drow = SURF
            .pixels
            .add(((dy + y) as u32 * SURF.pitch + dx as u32) as usize);
        let mut x = 0i32;
        while x < dw {
            let sx = ((x as i64) * (src_w as i64)) / (dw as i64);
            *drow.add(x as usize) = *srow.add(sx as usize);
            x += 1;
        }
        y += 1;
    }
    0
}

/// Detect + init + paint boot stripe (no serial spam).
pub unsafe fn detect_and_present() -> i32 {
    if harvest() != 0 {
        return 0; /* no FB — serial-only is fine */
    }
    if init() != 0 {
        return -1;
    }
    clear(rgb(0x20, 0x28, 0x40));
    fill_rect(32, 32, 200, 48, rgb(0x3a, 0x7a, 0xd0));
    draw_text(
        48,
        48,
        b"metal gfx\0".as_ptr(),
        rgb(0xff, 0xff, 0xff),
        rgb(0, 0, 0),
        1,
    );
    if present() != 0 {
        return -1;
    }
    #[cfg(pm_gfx_ui)]
    {
        let _ = ui::boot_stripe();
    }
    0
}
