//! pymergetic.metal.drivers — replaceable class cards (net, blk, rtc).
//! Closed product namespace like `metal.net`, not pep420 (`util` is the
//! open constellation root). C leaves live in metal.mk. This file is the
//! mechanical Rust barrel so `pymergetic::metal::drivers::{net,blk,rtc}` resolve.
//!
//! RS match macros (`PM_METAL_DRV_*_RS!`) emit the same `pm_metal_drv` section
//! as `PM_METAL_DRV_*_C` in `__types__.h`.
#[path = "drivers/net.rs"]
pub mod net;

#[path = "drivers/blk.rs"]
pub mod blk;

#[path = "drivers/rtc.rs"]
pub mod rtc;

pub const PM_METAL_DRV_KIND_PCI: u32 = 1;
pub const PM_METAL_DRV_KIND_ISA: u32 = 2;
pub const PM_METAL_DRV_KIND_PLATFORM: u32 = 3;
pub const PM_METAL_DRV_PCI_ANY: u32 = 0xffff_ffff;

#[repr(C, align(8))]
pub struct pm_metal_drv_t {
    pub module: *const u8,
    pub kind: u32,
    pub id0: u32,
    pub id1: u32,
    pub id2: u32,
    pub id3: u32,
    pub bar: u32,
    pub attach: Option<unsafe extern "C" fn(i32, u32, u32, u32, u32) -> i32>,
}

unsafe impl Sync for pm_metal_drv_t {}

#[repr(C, align(8))]
pub struct pm_metal_class_t {
    pub class_id: i32,
    pub unbind_dt: Option<unsafe extern "C" fn(i32) -> i32>,
}

unsafe impl Sync for pm_metal_class_t {}

unsafe extern "C" {
    pub fn pm_metal_drv_add(rec: *const pm_metal_drv_t) -> i32;
    pub fn pm_metal_class_add(rec: *const pm_metal_class_t) -> i32;
}

/// Same job as `PM_METAL_DRV_PCI_FULL_C`.
#[macro_export]
macro_rules! PM_METAL_DRV_PCI_FULL_RS {
    ($mod:expr, $vendor:expr, $device:expr, $pci_class:expr, $pci_rev:expr, $bar:expr, $attach:ident) => {
        const _: () = {
            #[used]
            #[unsafe(link_section = "pm_metal_drv")]
            static __PM_METAL_DRV: $crate::metal::drivers::pm_metal_drv_t =
                $crate::metal::drivers::pm_metal_drv_t {
                    module: concat!($mod, "\0").as_ptr(),
                    kind: $crate::metal::drivers::PM_METAL_DRV_KIND_PCI,
                    id0: $vendor as u32,
                    id1: $device as u32,
                    id2: $pci_class as u32,
                    id3: $pci_rev as u32,
                    bar: $bar as u32,
                    attach: Some($attach),
                };
            #[used]
            #[cfg_attr(
                any(target_arch = "wasm32", target_os = "linux", target_os = "emscripten"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_METAL_DRV_REG: extern "C" fn() = {
                extern "C" fn __pm_metal_drv_reg() {
                    unsafe {
                        let _ = $crate::metal::drivers::pm_metal_drv_add(&__PM_METAL_DRV);
                    }
                }
                __pm_metal_drv_reg
            };
        };
    };
}

#[macro_export]
macro_rules! PM_METAL_DRV_PCI_RS {
    ($mod:expr, $vendor:expr, $device:expr, $attach:ident) => {
        $crate::PM_METAL_DRV_PCI_FULL_RS!(
            $mod,
            $vendor,
            $device,
            $crate::metal::drivers::PM_METAL_DRV_PCI_ANY,
            $crate::metal::drivers::PM_METAL_DRV_PCI_ANY,
            $crate::metal::drivers::PM_METAL_DRV_PCI_ANY,
            $attach
        );
    };
}

#[macro_export]
macro_rules! PM_METAL_DRV_PCI_VENDOR_RS {
    ($mod:expr, $vendor:expr, $attach:ident) => {
        $crate::PM_METAL_DRV_PCI_RS!(
            $mod,
            $vendor,
            $crate::metal::drivers::PM_METAL_DRV_PCI_ANY,
            $attach
        );
    };
}

#[macro_export]
macro_rules! PM_METAL_DRV_ISA_RS {
    ($mod:expr, $port:expr, $attach:ident) => {
        const _: () = {
            #[used]
            #[unsafe(link_section = "pm_metal_drv")]
            static __PM_METAL_DRV: $crate::metal::drivers::pm_metal_drv_t =
                $crate::metal::drivers::pm_metal_drv_t {
                    module: concat!($mod, "\0").as_ptr(),
                    kind: $crate::metal::drivers::PM_METAL_DRV_KIND_ISA,
                    id0: $port as u32,
                    id1: 0,
                    id2: 0,
                    id3: 0,
                    bar: 0,
                    attach: Some($attach),
                };
            #[used]
            #[cfg_attr(
                any(target_arch = "wasm32", target_os = "linux", target_os = "emscripten"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_METAL_DRV_REG: extern "C" fn() = {
                extern "C" fn __pm_metal_drv_reg() {
                    unsafe {
                        let _ = $crate::metal::drivers::pm_metal_drv_add(&__PM_METAL_DRV);
                    }
                }
                __pm_metal_drv_reg
            };
        };
    };
}

#[macro_export]
macro_rules! PM_METAL_DRV_PLATFORM_RS {
    ($mod:expr, $attach:ident) => {
        const _: () = {
            #[used]
            #[unsafe(link_section = "pm_metal_drv")]
            static __PM_METAL_DRV: $crate::metal::drivers::pm_metal_drv_t =
                $crate::metal::drivers::pm_metal_drv_t {
                    module: concat!($mod, "\0").as_ptr(),
                    kind: $crate::metal::drivers::PM_METAL_DRV_KIND_PLATFORM,
                    id0: 0,
                    id1: 0,
                    id2: 0,
                    id3: 0,
                    bar: 0,
                    attach: Some($attach),
                };
            #[used]
            #[cfg_attr(
                any(target_arch = "wasm32", target_os = "linux", target_os = "emscripten"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_METAL_DRV_REG: extern "C" fn() = {
                extern "C" fn __pm_metal_drv_reg() {
                    unsafe {
                        let _ = $crate::metal::drivers::pm_metal_drv_add(&__PM_METAL_DRV);
                    }
                }
                __pm_metal_drv_reg
            };
        };
    };
}

#[macro_export]
macro_rules! PM_METAL_CLASS_RS {
    ($class_id:expr, $unbind:ident) => {
        const _: () = {
            #[used]
            #[unsafe(link_section = "pm_metal_class")]
            static __PM_METAL_CLASS: $crate::metal::drivers::pm_metal_class_t =
                $crate::metal::drivers::pm_metal_class_t {
                    class_id: $class_id as i32,
                    unbind_dt: Some($unbind),
                };
            #[used]
            #[cfg_attr(
                any(target_arch = "wasm32", target_os = "linux", target_os = "emscripten"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_METAL_CLASS_REG: extern "C" fn() = {
                extern "C" fn __pm_metal_class_reg() {
                    unsafe {
                        let _ = $crate::metal::drivers::pm_metal_class_add(&__PM_METAL_CLASS);
                    }
                }
                __pm_metal_class_reg
            };
        };
    };
}
