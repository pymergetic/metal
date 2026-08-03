//! Framebuffer harvest — EFI GOP stash, then Bochs/QEMU stdvga.

use core::ptr;

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
    fn pm_metal_bus_pci_enable_mem_bm(bus: u8, dev: u8, func: u8);
    fn pm_metal_boot_efi_gop_stash_get(
        fb_out: *mut *mut u32,
        w_out: *mut u32,
        h_out: *mut u32,
        ppsl_out: *mut u32,
        gop_out: *mut *mut core::ffi::c_void,
    ) -> i32;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_boot_io_ops() -> *const IoOps {
    ptr::null()
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_bus_pci_find(
    _: u16,
    _: u16,
    _: *mut u8,
    _: *mut u8,
    _: *mut u8,
) -> i32 {
    -1
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_bus_pci_bar_mmio(_: u8, _: u8, _: u8, _: u8, _: *mut u8) -> u64 {
    0
}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_bus_pci_enable_mem_bm(_: u8, _: u8, _: u8) {}
#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_boot_efi_gop_stash_get(
    _: *mut *mut u32,
    _: *mut u32,
    _: *mut u32,
    _: *mut u32,
    _: *mut *mut core::ffi::c_void,
) -> i32 {
    -1
}

const VBE_INDEX: u16 = 0x01CE;
const VBE_DATA: u16 = 0x01CF;

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

/// Harvest from EFI GOP stash (pre- or post-EBS).
/// `gop_out` is set only while Boot Services are alive.
pub unsafe fn harvest_efi_gop(
    fb_out: *mut *mut u32,
    w_out: *mut u32,
    h_out: *mut u32,
    ppsl_out: *mut u32,
    gop_out: *mut *mut core::ffi::c_void,
) -> i32 {
    if fb_out.is_null()
        || w_out.is_null()
        || h_out.is_null()
        || ppsl_out.is_null()
        || gop_out.is_null()
    {
        return -1;
    }
    pm_metal_boot_efi_gop_stash_get(fb_out, w_out, h_out, ppsl_out, gop_out)
}

/// Program Bochs DISPI to 1024x768x32 and return BAR0 as LFB.
pub unsafe fn harvest_bochs(
    fb_out: *mut *mut u32,
    w_out: *mut u32,
    h_out: *mut u32,
    ppsl_out: *mut u32,
) -> i32 {
    if fb_out.is_null() || w_out.is_null() || h_out.is_null() || ppsl_out.is_null() {
        return -1;
    }
    let mut bus = 0u8;
    let mut dev = 0u8;
    let mut func = 0u8;
    if pm_metal_bus_pci_find(0x1234, 0x1111, &mut bus, &mut dev, &mut func) != 0 {
        return -1;
    }
    let bar = pm_metal_bus_pci_bar_mmio(bus, dev, func, 0, ptr::null_mut());
    if bar == 0 || bar >= 0x1_0000_0000u64 {
        return -1;
    }
    pm_metal_bus_pci_enable_mem_bm(bus, dev, func);
    vbe_write(0, 0xB0C5);
    vbe_write(4, 0);
    vbe_write(1, 1024);
    vbe_write(2, 768);
    vbe_write(3, 32);
    vbe_write(4, 0x41);
    *fb_out = bar as *mut u32;
    *w_out = 1024;
    *h_out = 768;
    *ppsl_out = 1024;
    0
}

/// Prefer Bochs (QEMU stdvga) when present; else EFI GOP stash.
/// Returns owned=1 post-firmware / BIOS; owned=0 only with live GOP Blt.
pub unsafe fn harvest_any(
    fb_out: *mut *mut u32,
    w_out: *mut u32,
    h_out: *mut u32,
    ppsl_out: *mut u32,
    gop_out: *mut *mut core::ffi::c_void,
    owned_out: *mut i32,
) -> i32 {
    if owned_out.is_null() || gop_out.is_null() {
        return -1;
    }
    *gop_out = ptr::null_mut();
    *owned_out = 1;
    if harvest_bochs(fb_out, w_out, h_out, ppsl_out) == 0 {
        return 0;
    }
    let mut gop: *mut core::ffi::c_void = ptr::null_mut();
    if harvest_efi_gop(fb_out, w_out, h_out, ppsl_out, &mut gop) != 0 {
        return -1;
    }
    if (*w_out) < 320 || (*h_out) < 200 || (*fb_out).is_null() {
        return -1;
    }
    if !gop.is_null() {
        *gop_out = gop;
        *owned_out = 0;
    }
    0
}
