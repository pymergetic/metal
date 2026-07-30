//! Shared freestanding Rust runtime for Metal firmware crates.
//!
//! Panic / halt are one path for Rust + C (Py later via generated face).
//! Output goes through the console ring when ready (viewports drain it).
#![cfg_attr(target_os = "none", no_std)]

use core::fmt::Write;

#[cfg(target_os = "none")]
extern "C" {
    fn pm_metal_console_ready() -> i32;
    fn pm_metal_console_write(id: u32, s: *const u8, n: usize);
}

#[cfg(not(target_os = "none"))]
fn pm_metal_console_ready() -> i32 {
    0
}

#[cfg(not(target_os = "none"))]
fn pm_metal_console_write(_id: u32, _s: *const u8, _n: usize) {}

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
    #[cfg(target_os = "none")]
    unsafe {
        if pm_metal_console_ready() != 0 {
            pm_metal_console_write(0, bytes.as_ptr(), bytes.len());
        }
    }
    #[cfg(not(target_os = "none"))]
    if pm_metal_console_ready() != 0 {
        pm_metal_console_write(0, bytes.as_ptr(), bytes.len());
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
#[no_mangle]
pub extern "C" fn pm_metal_rt_halt() -> ! {
    halt_forever()
}

/// Fatal panic: print ASCII `msg` (NUL-terminated, may be null) then halt.
/// Same sink Rust `panic!` uses. Does not return.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_rt_panic(msg: *const u8) -> ! {
    pm_metal_rt_panic_at(core::ptr::null(), 0, msg)
}

/// Fatal panic with optional `file` + `line` (line 0 = omit). Does not return.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_rt_panic_at(
    file: *const u8,
    line: u32,
    msg: *const u8,
) -> ! {
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

#[cfg(target_os = "none")]
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
