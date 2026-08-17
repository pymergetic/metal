//! Firmware/emcc face of lock + registry + loader + version + metal ASGI.
//! Same `__impl__.rs` cards as unix cargo. These seats are hosts, so
//! `PM_MOD_EXPORT_RS!` registers here exactly as on unix; the registry stages
//! each record and replays it on first read, because `.init_array` /
//! `.CRT$XCU` run before the heap exists. `PM_MOD_BOOT_RS!` registers the
//! same way.
#![no_std]
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

extern crate alloc;

// On these seats this crate *is* the wasmmod face, so it answers to that name.
// The cards say `pymergetic_wasmmod::PM_MOD_BOOT_RS!` once and compile
// unchanged here and under cargo — no per-seat copy of a card's source.
extern crate self as pymergetic_wasmmod;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::c_void;

unsafe extern "C" {
    fn malloc(size: usize) -> *mut c_void;
    fn realloc(ptr: *mut c_void, size: usize) -> *mut c_void;
    fn free(ptr: *mut c_void);
}

struct LibcAlloc;

unsafe impl GlobalAlloc for LibcAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe { malloc(layout.size()) as *mut u8 }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        unsafe { free(ptr as *mut c_void) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, _layout: Layout, new_size: usize) -> *mut u8 {
        unsafe { realloc(ptr as *mut c_void, new_size) as *mut u8 }
    }
}

#[global_allocator]
static A: LibcAlloc = LibcAlloc;

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_eh_personality() {}

/// Same job as unix `PM_MOD_EXPORT_RS!`: stage the record from a constructor
/// and let the registry replay it once a heap exists. BIOS/RV1106 are
/// `target_os = "none"` and UEFI is PE, so the section list is wider than the
/// cargo build's.
#[macro_export]
macro_rules! PM_MOD_EXPORT_RS {
    ($fqn:expr, $fn:ident, $sig:expr) => {
        const _: () = {
            #[used]
            #[cfg_attr(
                any(
                    target_arch = "wasm32",
                    target_os = "none",
                    target_os = "linux",
                    target_os = "emscripten"
                ),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            static __PM_MOD_EXPORT: extern "C" fn() = {
                extern "C" fn __pm_mod_export_ctor() {
                    let fqn: &str = $fqn;
                    let name: &str = stringify!($fn);
                    let sig: &str = $sig;
                    let ptr = $fn as *mut core::ffi::c_void;
                    unsafe {
                        let _ = $crate::wasmmod::registry::pm_wasmmod_registry_mod_export(
                            fqn.as_ptr(),
                            fqn.len() as u32,
                            name.as_ptr(),
                            name.len() as u32,
                            $crate::wasmmod::registry::pm_wasmmod_registry_export_kind_t::Fn,
                            ptr,
                            sig.as_ptr(),
                            sig.len() as u32,
                        );
                    }
                }
                __pm_mod_export_ctor
            };
        };
    };
}

#[repr(C, align(8))]
pub struct pm_mod_boot_t {
    pub fqn: *const u8,
    pub init: Option<unsafe extern "C" fn(*mut c_void) -> i32>,
    pub deinit: Option<unsafe extern "C" fn()>,
    pub ready: Option<unsafe extern "C" fn() -> i32>,
}

unsafe impl Sync for pm_mod_boot_t {}

#[repr(C, align(8))]
pub struct pm_mod_bootdep_t {
    pub fqn: *const u8,
    pub dep: *const u8,
    pub flags: u32,
}

unsafe impl Sync for pm_mod_bootdep_t {}

unsafe extern "C" {
    fn pm_mod_boot_add(rec: *const pm_mod_boot_t) -> i32;
    fn pm_mod_bootdep_add(rec: *const pm_mod_bootdep_t) -> i32;
}

/// Same job as unix `PM_MOD_BOOT_RS!`. BIOS is `target_os = none` — must hit
/// `.init_array` or the card never boots.
#[macro_export]
macro_rules! PM_MOD_BOOT_RS {
    ($fqn:expr, $init:ident, $deinit:ident) => {
        const _: () = {
            unsafe extern "C" fn __pm_mod_boot_init(arena: *mut core::ffi::c_void) -> i32 {
                unsafe { $init(arena.cast()) }
            }
            #[used]
            static __PM_MOD_BOOT: $crate::pm_mod_boot_t = $crate::pm_mod_boot_t {
                fqn: concat!($fqn, "\0").as_ptr(),
                init: Some(__pm_mod_boot_init),
                deinit: Some($deinit),
                ready: None,
            };
            #[used]
            #[cfg_attr(
                any(
                    target_arch = "wasm32",
                    target_os = "none",
                    target_os = "linux",
                    target_os = "emscripten"
                ),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            static __PM_MOD_BOOT_REG: extern "C" fn() = {
                extern "C" fn __pm_mod_boot_reg() {
                    unsafe {
                        let _ = $crate::pm_mod_boot_add(&__PM_MOD_BOOT);
                    }
                }
                __pm_mod_boot_reg
            };
        };
    };
}

#[macro_export]
macro_rules! PM_MOD_BOOTDEP_RS {
    ($fqn:expr, $dep:expr) => {
        const _: () = {
            #[used]
            static __PM_MOD_BOOTDEP: $crate::pm_mod_bootdep_t = $crate::pm_mod_bootdep_t {
                fqn: concat!($fqn, "\0").as_ptr(),
                dep: concat!($dep, "\0").as_ptr(),
                flags: 0,
            };
            #[used]
            #[cfg_attr(
                any(
                    target_arch = "wasm32",
                    target_os = "none",
                    target_os = "linux",
                    target_os = "emscripten"
                ),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            static __PM_MOD_BOOTDEP_REG: extern "C" fn() = {
                extern "C" fn __pm_mod_bootdep_reg() {
                    unsafe {
                        let _ = $crate::pm_mod_bootdep_add(&__PM_MOD_BOOTDEP);
                    }
                }
                __pm_mod_bootdep_reg
            };
        };
    };
}

pub mod util;
pub mod wasmmod;

#[path = "../../src/pymergetic/metal/net/http/asgi/__impl__.rs"]
mod asgi;
