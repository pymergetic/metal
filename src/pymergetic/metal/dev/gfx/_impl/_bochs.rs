//! Bochs/QEMU stdvga scanout — VBE virt_h page-flip.

use core::ptr;

use crate::scanout::{self, Bind, Ops, CAP_DIRECT, CAP_TEAR_FREE};

const VBE_INDEX: u16 = 0x01CE;
const VBE_DATA: u16 = 0x01CF;
const VBE_ID: u16 = 0x0;
const VBE_XRES: u16 = 0x1;
const VBE_YRES: u16 = 0x2;
const VBE_ENABLE: u16 = 0x4;
const VBE_VIRT_W: u16 = 0x6;
const VBE_VIRT_H: u16 = 0x7;
const VBE_Y_OFF: u16 = 0x9;
const VBE_ENABLED: u16 = 0x01;
const VBE_LFB: u16 = 0x40;
const VBE_NOCLEAR: u16 = 0x80;
const VBE_ID0: u16 = 0xB0C0;

#[repr(C)]
struct IoOps {
    outb: Option<unsafe extern "C" fn(u16, u8)>,
    inb: Option<unsafe extern "C" fn(u16) -> u8>,
    out16: Option<unsafe extern "C" fn(u16, u16)>,
    in16: Option<unsafe extern "C" fn(u16) -> u16>,
    out32: Option<unsafe extern "C" fn(u16, u32)>,
    in32: Option<unsafe extern "C" fn(u16) -> u32>,
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_boot_io_ops() -> *const IoOps;
    fn pm_metal_bus_pci_find(
        vendor: u16,
        device: u16,
        bus_out: *mut u8,
        dev_out: *mut u8,
        func_out: *mut u8,
    ) -> i32;
    fn pm_metal_bus_pci_bar_mmio(
        bus: u8,
        dev: u8,
        func: u8,
        bar_index: u8,
        bars_consumed: *mut u8,
    ) -> u64;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_boot_io_ops() -> *const IoOps {
    core::ptr::null()
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_bus_pci_find(
    _v: u16,
    _d: u16,
    _b: *mut u8,
    _de: *mut u8,
    _f: *mut u8,
) -> i32 {
    -1
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_bus_pci_bar_mmio(
    _b: u8,
    _d: u8,
    _f: u8,
    _i: u8,
    _c: *mut u8,
) -> u64 {
    0
}

fn vbe_write(index: u16, value: u16) {
    unsafe {
        let ops = pm_metal_boot_io_ops();
        if ops.is_null() {
            return;
        }
        let Some(out16) = (*ops).out16 else {
            return;
        };
        out16(VBE_INDEX, index);
        out16(VBE_DATA, value);
    }
}

fn vbe_read(index: u16) -> u16 {
    unsafe {
        let ops = pm_metal_boot_io_ops();
        if ops.is_null() {
            return 0;
        }
        let Some(out16) = (*ops).out16 else {
            return 0;
        };
        let Some(in16) = (*ops).in16 else {
            return 0;
        };
        out16(VBE_INDEX, index);
        in16(VBE_DATA)
    }
}

static mut ARMED: i32 = 0;
static mut FRONT: u32 = 0;
static mut PAGE_PX: u32 = 0;

fn probe(b: &Bind) -> i32 {
    unsafe {
        ARMED = 0;
        FRONT = 0;
    }
    /* Pre-EBS (owned=0) keeps gop_blt; Bochs DISPI is the post-firmware path. */
    if b.owned == 0 || b.fb.is_null() || b.mode_w == 0 || b.mode_h == 0 {
        return -1;
    }
    let mut bus = 0u8;
    let mut dev = 0u8;
    let mut func = 0u8;
    unsafe {
        if pm_metal_bus_pci_find(0x1234, 0x1111, &mut bus, &mut dev, &mut func) != 0 {
            return -1;
        }
        let bar = pm_metal_bus_pci_bar_mmio(bus, dev, func, 0, ptr::null_mut());
        if bar == 0 || (bar as *mut u32) != b.fb {
            return -1;
        }
    }
    vbe_write(VBE_ID, 0xB0C5);
    let id = vbe_read(VBE_ID);
    if id < VBE_ID0 || id > VBE_ID0 + 6 {
        return -1;
    }
    let mut xres = vbe_read(VBE_XRES);
    let mut yres = vbe_read(VBE_YRES);
    if xres == 0 || yres == 0 {
        xres = b.mode_w as u16;
        yres = b.mode_h as u16;
    }
    if xres as u32 != b.mode_w || yres as u32 != b.mode_h {
        return -1;
    }
    let page_bytes = (b.mode_h as usize) * (b.fb_ppsl as usize) * 4;
    if page_bytes == 0 || page_bytes * 2 > 64 * 1024 * 1024 {
        return -1;
    }
    let virt_h = (b.mode_h * 2) as u16;
    vbe_write(VBE_VIRT_W, b.fb_ppsl as u16);
    vbe_write(VBE_VIRT_H, virt_h);
    vbe_write(VBE_Y_OFF, 0);
    vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB | VBE_NOCLEAR);
    if vbe_read(VBE_VIRT_H) < virt_h {
        return -1;
    }
    unsafe {
        ptr::copy_nonoverlapping(
            b.fb as *const u8,
            b.fb.add(b.mode_h as usize * b.fb_ppsl as usize) as *mut u8,
            page_bytes,
        );
        PAGE_PX = b.mode_h * b.fb_ppsl;
        ARMED = 1;
        FRONT = 0;
    }
    0
}

fn copy_outside(
    back: *mut u32,
    front: *const u32,
    pitch: u32,
    page_h: u32,
    mut x: i32,
    mut y: i32,
    mut w: i32,
    mut h: i32,
) {
    if back.is_null() || front.is_null() || pitch == 0 || page_h == 0 {
        return;
    }
    if x < 0 {
        w += x;
        x = 0;
    }
    if y < 0 {
        h += y;
        y = 0;
    }
    unsafe {
        if w <= 0 || h <= 0 || (x as u32) >= pitch || (y as u32) >= page_h {
            ptr::copy_nonoverlapping(
                front as *const u8,
                back as *mut u8,
                (page_h as usize) * (pitch as usize) * 4,
            );
            return;
        }
        let mut ww = w;
        let mut hh = h;
        if (x as u32) + (ww as u32) > pitch {
            ww = (pitch - x as u32) as i32;
        }
        if (y as u32) + (hh as u32) > page_h {
            hh = (page_h - y as u32) as i32;
        }
        let y0 = y as u32;
        let y1 = (y + hh) as u32;
        let row_bytes = (pitch as usize) * 4;
        if y0 > 0 {
            ptr::copy_nonoverlapping(front as *const u8, back as *mut u8, (y0 as usize) * row_bytes);
        }
        if y1 < page_h {
            ptr::copy_nonoverlapping(
                front.add((y1 * pitch) as usize) as *const u8,
                back.add((y1 * pitch) as usize) as *mut u8,
                ((page_h - y1) as usize) * row_bytes,
            );
        }
        let left_bytes = (x as usize) * 4;
        let right_off = (x + ww) as u32;
        let right_bytes = if right_off < pitch {
            ((pitch - right_off) as usize) * 4
        } else {
            0
        };
        for row in y0..y1 {
            let db = back.add((row * pitch) as usize);
            let sb = front.add((row * pitch) as usize);
            if left_bytes > 0 {
                ptr::copy_nonoverlapping(sb as *const u8, db as *mut u8, left_bytes);
            }
            if right_bytes > 0 {
                ptr::copy_nonoverlapping(
                    sb.add(right_off as usize) as *const u8,
                    db.add(right_off as usize) as *mut u8,
                    right_bytes,
                );
            }
        }
    }
}

fn present_rect(x: i32, y: i32, w: i32, h: i32) -> i32 {
    unsafe {
        if ARMED == 0 {
            return -1;
        }
        let b = scanout::bind_info();
        if b.fb.is_null() {
            return -1;
        }
        let back = 1u32 - FRONT;
        let back_base = b.fb.add((back * PAGE_PX) as usize);
        let front_base = b.fb.add((FRONT * PAGE_PX) as usize);
        let page_h = if b.fb_ppsl != 0 {
            PAGE_PX / b.fb_ppsl
        } else {
            0
        };
        let full = x == 0
            && y == 0
            && w == b.shadow_w as i32
            && h == b.shadow_h as i32;
        if b.shadow != back_base {
            if !full {
                copy_outside(back_base, front_base, b.fb_ppsl, page_h, x, y, w, h);
            }
            scanout::copy_rect(back_base, b.fb_ppsl, x, y, w, h, b);
        }
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        vbe_write(VBE_Y_OFF, (back * b.mode_h) as u16);
        FRONT = back;
        0
    }
}

fn job_begin(x: i32, y: i32, w: i32, h: i32) -> i32 {
    if present_rect(x, y, w, h) == 0 {
        0
    } else {
        -1
    }
}

fn job_step() -> i32 {
    0
}

fn caps() -> u32 {
    CAP_TEAR_FREE | CAP_DIRECT
}

fn adopt_shadow(pixels: *mut *mut u32, pitch: *mut u32) -> i32 {
    unsafe {
        if ARMED == 0 || pixels.is_null() {
            return -1;
        }
        let b = scanout::bind_info();
        if b.fb.is_null() {
            return -1;
        }
        let back = b.fb.add(((1u32 - FRONT) * PAGE_PX) as usize);
        *pixels = back;
        if !pitch.is_null() {
            *pitch = b.fb_ppsl;
        }
        scanout::bind_set_shadow(back, b.fb_ppsl);
        0
    }
}

fn after_flip(pixels: *mut *mut u32) {
    unsafe {
        if ARMED == 0 || pixels.is_null() {
            return;
        }
        let b = scanout::bind_info();
        if b.fb.is_null() {
            return;
        }
        let back = b.fb.add(((1u32 - FRONT) * PAGE_PX) as usize);
        *pixels = back;
        scanout::bind_set_shadow(back, b.fb_ppsl);
    }
}

fn fini() {
    unsafe {
        ARMED = 0;
    }
}

pub static OPS: Ops = Ops {
    name: "bochs_flip",
    probe,
    present_rect,
    job_begin,
    job_step,
    caps,
    adopt_shadow: Some(adopt_shadow),
    after_flip: Some(after_flip),
    fini,
};
