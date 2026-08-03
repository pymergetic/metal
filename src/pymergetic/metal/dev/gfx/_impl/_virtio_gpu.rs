//! virtio-gpu scanout — RESOURCE_FLUSH present (QEMU reference GPU path).

use core::mem::MaybeUninit;
use core::ptr::{self, addr_of_mut};

use crate::scanout::{self, Bind, Ops, CAP_DIRECT, CAP_TEAR_FREE};

const VIRTIO_DEV_GPU: u16 = 0x1050;
const VIRTIO_S_ACK: u8 = 1;
const VIRTIO_S_DRIVER: u8 = 2;
const VIRTIO_S_FEATURES: u8 = 8;
const VIRTIO_F_VERSION_1: u64 = 1u64 << 32;

const CMD_RESOURCE_CREATE_2D: u32 = 0x0101;
const CMD_SET_SCANOUT: u32 = 0x0103;
const CMD_RESOURCE_FLUSH: u32 = 0x0104;
const CMD_TRANSFER_TO_HOST_2D: u32 = 0x0105;
const CMD_RESOURCE_ATTACH_BACKING: u32 = 0x0106;
const FORMAT_B8G8R8X8_UNORM: u32 = 2;
const RESP_OK_NODATA: u32 = 0x1100;
const CMD_TIMEOUT_US: u64 = 500_000;

#[repr(C)]
struct Virtq {
    qidx: u16,
    size: u16,
    free_head: u16,
    num_free: u16,
    last_used: u16,
    notify_off: u16,
    desc: *mut core::ffi::c_void,
    avail: *mut core::ffi::c_void,
    used: *mut core::ffi::c_void,
    ring_mem: *mut core::ffi::c_void,
    ring_pages: u32,
    desc_phys: u64,
    avail_phys: u64,
    used_phys: u64,
    next: *mut u16,
}

#[repr(C)]
struct VirtioDev {
    pci_io: *mut core::ffi::c_void,
    handle: *mut core::ffi::c_void,
    pci_device_id: u16,
    common: *mut u8,
    notify: *mut u8,
    device_cfg: *mut u8,
    notify_off_mult: u32,
    common_bar: u32,
    notify_bar: u32,
    device_bar: u32,
    features: u64,
    vqs: *mut Virtq,
    n_vqs: u16,
    mmio: i32,
}

#[repr(C, packed)]
struct CtrlHdr {
    type_: u32,
    flags: u32,
    fence_id: u64,
    ctx_id: u32,
    padding: u32,
}

#[repr(C, packed)]
struct ResCreate2d {
    hdr: CtrlHdr,
    resource_id: u32,
    format: u32,
    width: u32,
    height: u32,
}

#[repr(C, packed)]
struct MemEntry {
    addr: u64,
    length: u32,
    padding: u32,
}

#[repr(C, packed)]
struct AttachBacking {
    hdr: CtrlHdr,
    resource_id: u32,
    nr_entries: u32,
}

#[repr(C, packed)]
struct Rect {
    x: i32,
    y: i32,
    w: u32,
    h: u32,
}

#[repr(C, packed)]
struct SetScanout {
    hdr: CtrlHdr,
    r: Rect,
    scanout_id: u32,
    resource_id: u32,
}

#[repr(C, packed)]
struct Transfer2d {
    hdr: CtrlHdr,
    r: Rect,
    offset: u64,
    resource_id: u32,
    padding: u32,
}

#[repr(C, packed)]
struct ResourceFlush {
    hdr: CtrlHdr,
    r: Rect,
    resource_id: u32,
    padding: u32,
}

#[repr(C, packed)]
struct Resp {
    hdr: CtrlHdr,
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_virtio_find(pci_device_id: u16) -> i32;
    fn pm_metal_virtio_open(pci_device_id: u16, out: *mut VirtioDev) -> i32;
    fn pm_metal_virtio_close(dev: *mut VirtioDev);
    fn pm_metal_virtio_get_features(dev: *mut VirtioDev) -> u64;
    fn pm_metal_virtio_set_features(dev: *mut VirtioDev, features: u64) -> i32;
    fn pm_metal_virtio_set_status(dev: *mut VirtioDev, status: u8);
    fn pm_metal_virtio_setup_queue(dev: *mut VirtioDev, qidx: u16, want_size: u16) -> i32;
    fn pm_metal_virtio_driver_ok(dev: *mut VirtioDev) -> i32;
    fn pm_metal_virtio_pages_alloc(pages: u32) -> *mut u8;
    fn pm_metal_virtio_pages_free(buf: *mut u8, pages: u32);
    fn pm_metal_virtq_add2(
        vq: *mut Virtq,
        buf0: *mut core::ffi::c_void,
        len0: u32,
        write0: i32,
        buf1: *mut core::ffi::c_void,
        len1: u32,
        write1: i32,
        head_out: *mut u16,
    ) -> i32;
    fn pm_metal_virtq_kick(dev: *mut VirtioDev, vq: *mut Virtq);
    fn pm_metal_virtq_get_used(vq: *mut Virtq, head: *mut u16, len: *mut u32) -> i32;
    fn pm_metal_virtq_free_chain(vq: *mut Virtq, head: u16);
    fn pm_metal_mem_alloc(size: usize) -> *mut u8;
    fn pm_metal_mem_free(ptr: *mut u8);
    fn pm_metal_time_mono_us() -> u64;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_find(_: u16) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_open(_: u16, _: *mut VirtioDev) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_close(_: *mut VirtioDev) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_get_features(_: *mut VirtioDev) -> u64 {
    0
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_set_features(_: *mut VirtioDev, _: u64) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_set_status(_: *mut VirtioDev, _: u8) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_setup_queue(_: *mut VirtioDev, _: u16, _: u16) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_driver_ok(_: *mut VirtioDev) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_pages_alloc(_: u32) -> *mut u8 {
    ptr::null_mut()
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtio_pages_free(_: *mut u8, _: u32) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtq_add2(
    _: *mut Virtq,
    _: *mut core::ffi::c_void,
    _: u32,
    _: i32,
    _: *mut core::ffi::c_void,
    _: u32,
    _: i32,
    _: *mut u16,
) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtq_kick(_: *mut VirtioDev, _: *mut Virtq) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtq_get_used(_: *mut Virtq, _: *mut u16, _: *mut u32) -> i32 {
    0
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_virtq_free_chain(_: *mut Virtq, _: u16) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_mem_alloc(_: usize) -> *mut u8 {
    ptr::null_mut()
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_mem_free(_: *mut u8) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_time_mono_us() -> u64 {
    0
}

fn cpu_pause() {
    #[cfg(any(target_arch = "x86_64", target_arch = "x86"))]
    unsafe {
        core::arch::asm!("pause", options(nomem, nostack));
    }
}

struct Vgpu {
    ready: i32,
    opened: i32,
    res_id: u32,
    w: u32,
    h: u32,
    cmd_buf: *mut u8,
    resp_buf: *mut u8,
    cmd_cap: u32,
    dev: MaybeUninit<VirtioDev>,
}

static mut VG: Vgpu = Vgpu {
    ready: 0,
    opened: 0,
    res_id: 0,
    w: 0,
    h: 0,
    cmd_buf: ptr::null_mut(),
    resp_buf: ptr::null_mut(),
    cmd_cap: 0,
    dev: MaybeUninit::uninit(),
};

unsafe fn vg() -> &'static mut Vgpu {
    &mut *addr_of_mut!(VG)
}

unsafe fn vgpu_cmd(cmd: *const u8, cmd_len: u32) -> i32 {
    let g = vg();
    if g.ready == 0 || cmd.is_null() || cmd_len == 0 || cmd_len > g.cmd_cap {
        return -1;
    }
    let dev = g.dev.as_mut_ptr();
    if (*dev).vqs.is_null() || (*dev).n_vqs == 0 {
        return -1;
    }
    let vq = (*dev).vqs;
    ptr::copy_nonoverlapping(cmd, g.cmd_buf, cmd_len as usize);
    ptr::write_bytes(g.resp_buf, 0, core::mem::size_of::<Resp>());
    let mut head = 0u16;
    if pm_metal_virtq_add2(
        vq,
        g.cmd_buf as *mut core::ffi::c_void,
        cmd_len,
        0,
        g.resp_buf as *mut core::ffi::c_void,
        core::mem::size_of::<Resp>() as u32,
        1,
        &mut head,
    ) != 0
    {
        return -1;
    }
    pm_metal_virtq_kick(dev, vq);
    let deadline = pm_metal_time_mono_us().wrapping_add(CMD_TIMEOUT_US);
    while pm_metal_time_mono_us() < deadline {
        let mut uh = 0u16;
        let mut ul = 0u32;
        if pm_metal_virtq_get_used(vq, &mut uh, &mut ul) != 0 {
            pm_metal_virtq_free_chain(vq, uh);
            let resp = g.resp_buf as *const Resp;
            let ty = core::ptr::read_unaligned(core::ptr::addr_of!((*resp).hdr.type_));
            return if ty == RESP_OK_NODATA { 0 } else { -1 };
        }
        cpu_pause();
    }
    -1
}

unsafe fn vgpu_fail() {
    let g = vg();
    if !g.cmd_buf.is_null() {
        pm_metal_virtio_pages_free(g.cmd_buf, 1);
        g.cmd_buf = ptr::null_mut();
    }
    if !g.resp_buf.is_null() {
        pm_metal_virtio_pages_free(g.resp_buf, 1);
        g.resp_buf = ptr::null_mut();
    }
    if g.opened != 0 {
        pm_metal_virtio_close(g.dev.as_mut_ptr());
        g.opened = 0;
    }
    g.ready = 0;
    g.cmd_cap = 0;
    g.res_id = 0;
    g.w = 0;
    g.h = 0;
}

fn probe(b: &Bind) -> i32 {
    unsafe {
        vgpu_fail();
        if b.shadow.is_null() || b.mode_w == 0 || b.mode_h == 0 {
            return -1;
        }
        if pm_metal_virtio_find(VIRTIO_DEV_GPU) != 0 {
            return -1;
        }
        let g = vg();
        ptr::write_bytes(g.dev.as_mut_ptr() as *mut u8, 0, core::mem::size_of::<VirtioDev>());
        if pm_metal_virtio_open(VIRTIO_DEV_GPU, g.dev.as_mut_ptr()) != 0 {
            return -1;
        }
        g.opened = 1;
        let dev = g.dev.as_mut_ptr();
        pm_metal_virtio_set_status(dev, VIRTIO_S_ACK);
        pm_metal_virtio_set_status(dev, VIRTIO_S_DRIVER);
        let feats = pm_metal_virtio_get_features(dev) & VIRTIO_F_VERSION_1;
        if pm_metal_virtio_set_features(dev, feats) != 0 {
            let _ = pm_metal_virtio_set_features(dev, 0);
        }
        pm_metal_virtio_set_status(dev, VIRTIO_S_FEATURES);
        if pm_metal_virtio_setup_queue(dev, 0, 64) != 0 {
            vgpu_fail();
            return -1;
        }
        /* cursorq — QEMU virtio-gpu expects both queues before DRIVER_OK */
        if pm_metal_virtio_setup_queue(dev, 1, 16) != 0 {
            vgpu_fail();
            return -1;
        }
        if pm_metal_virtio_driver_ok(dev) != 0 {
            vgpu_fail();
            return -1;
        }
        g.cmd_cap = 4096;
        g.cmd_buf = pm_metal_virtio_pages_alloc(1);
        g.resp_buf = pm_metal_virtio_pages_alloc(1);
        if g.cmd_buf.is_null() || g.resp_buf.is_null() {
            vgpu_fail();
            return -1;
        }
        g.w = b.mode_w;
        g.h = b.mode_h;
        g.res_id = 1;
        g.ready = 1; /* allow vgpu_cmd during setup */

        let create = ResCreate2d {
            hdr: CtrlHdr {
                type_: CMD_RESOURCE_CREATE_2D,
                flags: 0,
                fence_id: 0,
                ctx_id: 0,
                padding: 0,
            },
            resource_id: g.res_id,
            format: FORMAT_B8G8R8X8_UNORM,
            width: g.w,
            height: g.h,
        };
        if vgpu_cmd(
            &create as *const ResCreate2d as *const u8,
            core::mem::size_of::<ResCreate2d>() as u32,
        ) != 0
        {
            vgpu_fail();
            return -1;
        }

        /* One entry for the whole shadow — per-page lists overflow the 4K cmd buf. */
        let fb_bytes = g.w * g.h * 4;
        let attach_bytes =
            core::mem::size_of::<AttachBacking>() + core::mem::size_of::<MemEntry>();
        let attach_buf = pm_metal_mem_alloc(attach_bytes);
        if attach_buf.is_null() {
            vgpu_fail();
            return -1;
        }
        ptr::write_bytes(attach_buf, 0, attach_bytes);
        let attach = attach_buf as *mut AttachBacking;
        let ent = attach_buf.add(core::mem::size_of::<AttachBacking>()) as *mut MemEntry;
        (*attach).hdr.type_ = CMD_RESOURCE_ATTACH_BACKING;
        (*attach).resource_id = g.res_id;
        (*attach).nr_entries = 1;
        (*ent).addr = b.shadow as u64;
        (*ent).length = fb_bytes;
        let rc = vgpu_cmd(attach_buf, attach_bytes as u32);
        pm_metal_mem_free(attach_buf);
        if rc != 0 {
            vgpu_fail();
            return -1;
        }

        let scan = SetScanout {
            hdr: CtrlHdr {
                type_: CMD_SET_SCANOUT,
                flags: 0,
                fence_id: 0,
                ctx_id: 0,
                padding: 0,
            },
            r: Rect {
                x: 0,
                y: 0,
                w: g.w,
                h: g.h,
            },
            scanout_id: 0,
            resource_id: g.res_id,
        };
        if vgpu_cmd(
            &scan as *const SetScanout as *const u8,
            core::mem::size_of::<SetScanout>() as u32,
        ) != 0
        {
            vgpu_fail();
            return -1;
        }
        0
    }
}

fn present_rect(mut x: i32, mut y: i32, mut w: i32, mut h: i32) -> i32 {
    unsafe {
        let g = vg();
        if g.ready == 0 {
            return -1;
        }
        let b = scanout::bind_info();
        if b.shadow.is_null() {
            return -1;
        }
        if x < 0 {
            w += x;
            x = 0;
        }
        if y < 0 {
            h += y;
            y = 0;
        }
        if w <= 0 || h <= 0 {
            return 0;
        }
        let xfer = Transfer2d {
            hdr: CtrlHdr {
                type_: CMD_TRANSFER_TO_HOST_2D,
                flags: 0,
                fence_id: 0,
                ctx_id: 0,
                padding: 0,
            },
            r: Rect {
                x,
                y,
                w: w as u32,
                h: h as u32,
            },
            offset: ((y as u64) * (b.shadow_pitch as u64) + (x as u64)) * 4,
            resource_id: g.res_id,
            padding: 0,
        };
        if vgpu_cmd(
            &xfer as *const Transfer2d as *const u8,
            core::mem::size_of::<Transfer2d>() as u32,
        ) != 0
        {
            return -1;
        }
        let flush = ResourceFlush {
            hdr: CtrlHdr {
                type_: CMD_RESOURCE_FLUSH,
                flags: 0,
                fence_id: 0,
                ctx_id: 0,
                padding: 0,
            },
            r: xfer.r,
            resource_id: g.res_id,
            padding: 0,
        };
        vgpu_cmd(
            &flush as *const ResourceFlush as *const u8,
            core::mem::size_of::<ResourceFlush>() as u32,
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
    CAP_TEAR_FREE | CAP_DIRECT
}

fn adopt_shadow(pixels: *mut *mut u32, pitch: *mut u32) -> i32 {
    unsafe {
        if vg().ready == 0 || pixels.is_null() {
            return -1;
        }
        let b = scanout::bind_info();
        if b.shadow.is_null() {
            return -1;
        }
        *pixels = b.shadow;
        if !pitch.is_null() {
            *pitch = b.shadow_pitch;
        }
        0
    }
}

fn fini() {
    unsafe {
        vgpu_fail();
    }
}

pub static OPS: Ops = Ops {
    name: "virtio_gpu",
    probe,
    present_rect,
    job_begin,
    job_step,
    caps,
    adopt_shadow: Some(adopt_shadow),
    after_flip: None,
    fini,
};
