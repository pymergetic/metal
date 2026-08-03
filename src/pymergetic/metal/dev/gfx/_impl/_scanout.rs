//! Scanout dispatch — probe order, bind, shared shadow→FB copy.

use core::ptr;

use crate::bochs;
use crate::gop;
use crate::i915;
use crate::lfb;
use crate::radeon;
use crate::virtio_gpu;

pub const CAP_TEAR_FREE: u32 = 1 << 0;
pub const CAP_CHUNKED: u32 = 1 << 1;
pub const CAP_DIRECT: u32 = 1 << 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bind {
    pub shadow: *mut u32,
    pub shadow_w: u32,
    pub shadow_h: u32,
    pub shadow_pitch: u32,
    pub fb: *mut u32,
    pub fb_ppsl: u32,
    pub mode_w: u32,
    pub mode_h: u32,
    pub gop: *mut core::ffi::c_void,
    pub owned: i32,
}

impl Bind {
    pub const fn empty() -> Self {
        Self {
            shadow: ptr::null_mut(),
            shadow_w: 0,
            shadow_h: 0,
            shadow_pitch: 0,
            fb: ptr::null_mut(),
            fb_ppsl: 0,
            mode_w: 0,
            mode_h: 0,
            gop: ptr::null_mut(),
            owned: 0,
        }
    }
}

pub type ProbeFn = fn(&Bind) -> i32;
pub type PresentRectFn = fn(i32, i32, i32, i32) -> i32;
pub type JobBeginFn = fn(i32, i32, i32, i32) -> i32;
pub type JobStepFn = fn() -> i32;
pub type CapsFn = fn() -> u32;
pub type AdoptFn = fn(*mut *mut u32, *mut u32) -> i32;
pub type AfterFlipFn = fn(*mut *mut u32);
pub type FiniFn = fn();

pub struct Ops {
    pub name: &'static str,
    pub probe: ProbeFn,
    pub present_rect: PresentRectFn,
    pub job_begin: JobBeginFn,
    pub job_step: JobStepFn,
    pub caps: CapsFn,
    pub adopt_shadow: Option<AdoptFn>,
    pub after_flip: Option<AfterFlipFn>,
    pub fini: FiniFn,
}

static mut BIND: Bind = Bind::empty();
static mut OPS: Option<&'static Ops> = None;

fn collect_probe_order(out: &mut [&'static Ops; 6]) -> usize {
    let mut n = 0usize;
    #[cfg(pm_gfx_virtio)]
    {
        out[n] = &virtio_gpu::OPS;
        n += 1;
    }
    #[cfg(pm_gfx_bochs)]
    {
        out[n] = &bochs::OPS;
        n += 1;
    }
    #[cfg(pm_gfx_radeon)]
    {
        out[n] = &radeon::OPS;
        n += 1;
    }
    #[cfg(pm_gfx_i915)]
    {
        out[n] = &i915::OPS;
        n += 1;
    }
    #[cfg(pm_gfx_gop)]
    {
        out[n] = &gop::OPS;
        n += 1;
    }
    #[cfg(pm_gfx_lfb)]
    {
        out[n] = &lfb::OPS;
        n += 1;
    }
    /* Host smoke / missing autoconf: at least try lfb after qemu defaults. */
    #[cfg(not(any(
        pm_gfx_virtio,
        pm_gfx_bochs,
        pm_gfx_radeon,
        pm_gfx_i915,
        pm_gfx_gop,
        pm_gfx_lfb
    )))]
    {
        out[n] = &virtio_gpu::OPS;
        n += 1;
        out[n] = &bochs::OPS;
        n += 1;
        out[n] = &radeon::OPS;
        n += 1;
        out[n] = &i915::OPS;
        n += 1;
        out[n] = &gop::OPS;
        n += 1;
        out[n] = &lfb::OPS;
        n += 1;
    }
    n
}

pub fn copy_rect(dst: *mut u32, dst_pitch: u32, x: i32, y: i32, w: i32, h: i32, b: &Bind) {
    if dst.is_null() || b.shadow.is_null() || w <= 0 || h <= 0 {
        return;
    }
    let bytes = (w as usize) * core::mem::size_of::<u32>();
    unsafe {
        if x == 0
            && (w as u32) == b.shadow_w
            && dst_pitch == b.shadow_pitch
            && (w as u32) == dst_pitch
        {
            let n = bytes * (h as usize);
            ptr::copy_nonoverlapping(
                b.shadow.add((y as u32 * b.shadow_pitch) as usize) as *const u8,
                dst.add((y as u32 * dst_pitch) as usize) as *mut u8,
                n,
            );
            return;
        }
        for row in 0..h {
            let sy = (y + row) as u32;
            let dy = sy;
            ptr::copy_nonoverlapping(
                b.shadow.add((sy * b.shadow_pitch + x as u32) as usize) as *const u8,
                dst.add((dy * dst_pitch + x as u32) as usize) as *mut u8,
                bytes,
            );
        }
    }
}

pub fn bind(b: &Bind) -> i32 {
    unsafe {
        if let Some(ops) = OPS {
            (ops.fini)();
        }
        OPS = None;
        BIND = *b;
        let snap = BIND;
        let mut order: [&'static Ops; 6] = [&lfb::OPS; 6];
        let n = collect_probe_order(&mut order);
        for ops in order.iter().take(n) {
            if (ops.probe)(&snap) == 0 {
                OPS = Some(*ops);
                return 0;
            }
        }
    }
    -1
}

pub fn ops() -> Option<&'static Ops> {
    unsafe { OPS }
}

pub fn name() -> &'static str {
    match ops() {
        Some(o) => o.name,
        None => "none",
    }
}

pub fn caps() -> u32 {
    match ops() {
        Some(o) => (o.caps)(),
        None => 0,
    }
}

pub fn fini() {
    unsafe {
        if let Some(ops) = OPS {
            (ops.fini)();
        }
        OPS = None;
    }
}

pub fn bind_info() -> &'static Bind {
    unsafe { &*ptr::addr_of!(BIND) }
}

/// C ABI for HW scanout ports (`_scanout_hw.h`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_gfx_scanout_bind_info() -> *const Bind {
    ptr::addr_of!(BIND)
}

pub fn bind_set_shadow(pixels: *mut u32, pitch: u32) {
    unsafe {
        BIND.shadow = pixels;
        BIND.shadow_pitch = pitch;
    }
}

pub fn present_rect(x: i32, y: i32, w: i32, h: i32) -> i32 {
    match ops() {
        Some(o) => (o.present_rect)(x, y, w, h),
        None => -1,
    }
}
