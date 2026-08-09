//! Shared freestanding Rust runtime for Metal firmware crates.
//!
//! Panic / halt are one path for Rust + C (Py later via generated face).
//! Output goes through the console ring when ready (viewports drain it).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use core::fmt::Write;

mod ffi;

struct BufWriter {
    buf: [u8; 256],
    pos: usize,
}

impl BufWriter {
    const fn new() -> Self {
        Self {
            buf: [0; 256],
            pos: 0,
        }
    }

    fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.pos]
    }
}

impl Write for BufWriter {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &b in s.as_bytes() {
            if self.pos >= self.buf.len() {
                break;
            }
            if b < 0x80 {
                self.buf[self.pos] = b;
                self.pos += 1;
            }
        }
        Ok(())
    }
}

fn emergency_write(bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    /* Pre-console: silent (no hardware bypass). */
    unsafe {
        if ffi::console_ready() != 0 {
            ffi::console_write(0, bytes.as_ptr(), bytes.len());
        }
    }
}

fn cstr_bytes(p: *const u8) -> &'static [u8] {
    if p.is_null() {
        return &[];
    }
    let mut n = 0usize;
    unsafe {
        while n < 512 && *p.add(n) != 0 {
            n += 1;
        }
        core::slice::from_raw_parts(p, n)
    }
}

fn halt_forever() -> ! {
    #[cfg(target_arch = "x86_64")]
    loop {
        unsafe {
            core::arch::asm!("hlt", options(nomem, nostack));
        }
    }
    #[cfg(not(target_arch = "x86_64"))]
    loop {
        core::hint::spin_loop();
    }
}

/// Halt the CPU. Does not return.
#[cfg(feature = "export_c_abi")]
#[no_mangle]
pub extern "C" fn pm_metal_rt_halt() -> ! {
    halt_forever()
}

#[cfg(not(feature = "export_c_abi"))]
fn pm_metal_rt_halt() -> ! {
    halt_forever()
}

/// Fatal panic: print ASCII `msg` (NUL-terminated, may be null) then halt.
/// Same sink Rust `panic!` uses. Does not return.
#[cfg(feature = "export_c_abi")]
#[no_mangle]
pub unsafe extern "C" fn pm_metal_rt_panic(msg: *const u8) -> ! {
    pm_metal_rt_panic_at(core::ptr::null(), 0, msg)
}

#[cfg(not(feature = "export_c_abi"))]
unsafe fn pm_metal_rt_panic(msg: *const u8) -> ! {
    pm_metal_rt_panic_at(core::ptr::null(), 0, msg)
}

/// Fatal panic with optional `file` + `line` (line 0 = omit). Does not return.
#[cfg(feature = "export_c_abi")]
#[no_mangle]
pub unsafe extern "C" fn pm_metal_rt_panic_at(
    file: *const u8,
    line: u32,
    msg: *const u8,
) -> ! {
    panic_at_body(file, line, msg)
}

#[cfg(not(feature = "export_c_abi"))]
unsafe fn pm_metal_rt_panic_at(file: *const u8, line: u32, msg: *const u8) -> ! {
    panic_at_body(file, line, msg)
}

unsafe fn panic_at_body(file: *const u8, line: u32, msg: *const u8) -> ! {
    let mut w = BufWriter::new();
    let _ = w.write_str("panic: ");
    let file_b = cstr_bytes(file);
    if !file_b.is_empty() && line != 0 {
        for &b in file_b {
            if w.pos >= w.buf.len() {
                break;
            }
            if b < 0x80 {
                w.buf[w.pos] = b;
                w.pos += 1;
            }
        }
        let _ = write!(w, ":{}: ", line);
    }
    let msg_b = cstr_bytes(msg);
    if !msg_b.is_empty() {
        for &b in msg_b {
            if w.pos >= w.buf.len() {
                break;
            }
            if b < 0x80 {
                w.buf[w.pos] = b;
                w.pos += 1;
            }
        }
    }
    let _ = w.write_str("\n");
    emergency_write(w.as_bytes());
    halt_forever()
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
#[panic_handler]
fn rust_panic(info: &core::panic::PanicInfo) -> ! {
    let mut w = BufWriter::new();
    let _ = write!(w, "{}", info.message());
    /* NUL-terminated copies — Rust &str is not a C string. */
    let mut msg = [0u8; 256];
    let n = w.as_bytes().len().min(msg.len() - 1);
    msg[..n].copy_from_slice(&w.as_bytes()[..n]);

    let mut file = [0u8; 128];
    let (file_ptr, line) = if let Some(loc) = info.location() {
        let fb = loc.file().as_bytes();
        let fn_ = fb.len().min(file.len() - 1);
        file[..fn_].copy_from_slice(&fb[..fn_]);
        (file.as_ptr(), loc.line())
    } else {
        (core::ptr::null(), 0u32)
    };

    unsafe { pm_metal_rt_panic_at(file_ptr, line, msg.as_ptr()) }
}

/// Register rt's own dynamically-callable exports onto the registry
/// (`pymergetic.metal.rt.*`) — for late/unloadable callers only.
#[cfg(feature = "export_c_abi")]
#[no_mangle]
pub unsafe extern "C" fn pm_metal_rt_register_symbols() -> i32 {
    ffi::register_symbols()
}

/// No-op: `console`/`mem` are reached via direct `extern "C"` linkage (see
/// `ffi.rs`), not a runtime bind step. Kept for the stable C ABI
/// product bringup calls.
#[cfg(feature = "export_c_abi")]
#[no_mangle]
pub unsafe extern "C" fn pm_metal_rt_connect_symbols() -> i32 {
    ffi::connect_symbols()
}

/// Rust `alloc` → `pm_metal_mem_*` (implemented in mem; after `pm_metal_mem_init`
/// and `pm_metal_rt_connect_symbols`).
#[cfg(any(target_os = "none", target_os = "uefi"))]
mod rust_alloc {
    use core::alloc::{GlobalAlloc, Layout};

    use crate::ffi;

    struct MetalGlobalAlloc;

    unsafe impl GlobalAlloc for MetalGlobalAlloc {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            if layout.size() == 0 {
                return core::ptr::null_mut();
            }
            ffi::mem_memalign(layout.align().max(1), layout.size())
        }

        unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
            ffi::mem_free(ptr);
        }

        unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
            let _ = layout;
            ffi::mem_realloc(ptr, new_size)
        }
    }

    #[global_allocator]
    static GLOBAL: MetalGlobalAlloc = MetalGlobalAlloc;
}
