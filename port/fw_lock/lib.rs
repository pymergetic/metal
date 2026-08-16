//! Firmware/emcc face of lock + registry + loader + version.
//! Same `__impl__.rs` cards as unix cargo. `PM_MOD_EXPORT_RS!` is a no-op:
//! these seats are `-DPM_WASMMOD_GUEST=1` (no GNU start/stop walk).
#![no_std]
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

extern crate alloc;

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

#[macro_export]
macro_rules! PM_MOD_EXPORT_RS {
    ($($t:tt)*) => {};
}

pub mod util;
pub mod wasmmod;
