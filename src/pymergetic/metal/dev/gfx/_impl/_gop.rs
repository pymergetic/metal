//! Pre-EBS EFI GOP Blt scanout (Boot Services still live).

use core::ptr;

use crate::scanout::{self, Bind, Ops};

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_boot_gop_port_blt(
        gop: *mut core::ffi::c_void,
        src: *const u32,
        src_x: u32,
        src_y: u32,
        dst_x: u32,
        dst_y: u32,
        w: u32,
        h: u32,
        delta: u32,
    ) -> i32;
    fn pm_metal_mem_alloc(size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_boot_gop_port_blt(
    _: *mut core::ffi::c_void,
    _: *const u32,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_mem_alloc(_: usize) -> *mut u8 {
    ptr::null_mut()
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_mem_free(_: *mut u8) {}

static mut PACK: *mut u32 = ptr::null_mut();
static mut PACK_CAP: u32 = 0;

fn probe(b: &Bind) -> i32 {
    if b.gop.is_null() || b.owned != 0 {
        -1
    } else {
        0
    }
}

fn present_rect(x: i32, y: i32, w: i32, h: i32) -> i32 {
    unsafe {
        let b = scanout::bind_info();
        if b.gop.is_null() || b.shadow.is_null() || w <= 0 || h <= 0 {
            return -1;
        }
        if x == 0 && (w as u32) == b.shadow_w {
            return pm_metal_boot_gop_port_blt(
                b.gop,
                b.shadow,
                0,
                y as u32,
                0,
                y as u32,
                w as u32,
                h as u32,
                b.shadow_pitch * 4,
            );
        }
        let need = (w as u32) * (h as u32);
        if PACK.is_null() || PACK_CAP < need {
            if !PACK.is_null() {
                pm_metal_mem_free(PACK as *mut u8);
                PACK = ptr::null_mut();
                PACK_CAP = 0;
            }
            let p = pm_metal_mem_alloc((need as usize) * 4);
            if p.is_null() {
                return -1;
            }
            PACK = p as *mut u32;
            PACK_CAP = need;
        }
        for row in 0..h {
            ptr::copy_nonoverlapping(
                b.shadow
                    .add(((y + row) as u32 * b.shadow_pitch + x as u32) as usize),
                PACK.add((row as u32 * w as u32) as usize),
                w as usize,
            );
        }
        pm_metal_boot_gop_port_blt(
            b.gop,
            PACK,
            0,
            0,
            x as u32,
            y as u32,
            w as u32,
            h as u32,
            0,
        )
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
    0
}

fn fini() {
    unsafe {
        if !PACK.is_null() {
            pm_metal_mem_free(PACK as *mut u8);
            PACK = ptr::null_mut();
            PACK_CAP = 0;
        }
    }
}

pub static OPS: Ops = Ops {
    name: "gop_blt",
    probe,
    present_rect,
    job_begin,
    job_step,
    caps,
    adopt_shadow: None,
    after_flip: None,
    fini,
};
