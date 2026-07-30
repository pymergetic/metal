//! Welcome banner — FIGlet "METAL" + `Metal exp2 @ <cpu>`.

use core::fmt::Write;

use pymergetic_metal_log::{pm_metal_log_style_t, pm_metal_log_styled};

extern "C" {
    fn pm_metal_util_ascii_render(
        text: *const u8,
        cb: Option<unsafe extern "C" fn(ctx: *mut u8, s: *const u8, n: usize) -> i32>,
        ctx: *mut u8,
    ) -> i32;
}

unsafe extern "C" fn log_row(ctx: *mut u8, s: *const u8, n: usize) -> i32 {
    let _ = ctx;
    if s.is_null() || n == 0 {
        return 0;
    }
    let mut buf = [0u8; 192];
    let take = if n + 1 < buf.len() { n } else { buf.len() - 1 };
    for i in 0..take {
        let b = *s.add(i);
        buf[i] = if b < 0x80 { b } else { b'?' };
    }
    buf[take] = 0;
    pm_metal_log_styled(
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT,
        buf.as_ptr(),
    );
    0
}

struct LineBuf {
    buf: [u8; 128],
    pos: usize,
}

impl LineBuf {
    fn new() -> Self {
        Self {
            buf: [0; 128],
            pos: 0,
        }
    }

    fn as_cstr(&mut self) -> *const u8 {
        if self.pos >= self.buf.len() {
            self.pos = self.buf.len() - 1;
        }
        self.buf[self.pos] = 0;
        self.buf.as_ptr()
    }
}

impl Write for LineBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &b in s.as_bytes() {
            if self.pos + 1 >= self.buf.len() {
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

fn cpuid_brand(out: &mut [u8; 49]) {
    /* CPUID 0x80000002..04 -> 48 ASCII bytes + NUL. */
    out.fill(0);
    let mut ok = false;
    #[cfg(target_arch = "x86_64")]
    {
        use core::arch::x86_64::{__cpuid, __get_cpuid_max};
        let max_ext = __get_cpuid_max(0x8000_0000).0;
        if max_ext >= 0x8000_0004 {
            ok = true;
            let mut o = 0usize;
            for leaf in 0x8000_0002u32..=0x8000_0004u32 {
                let r = __cpuid(leaf);
                for w in [r.eax, r.ebx, r.ecx, r.edx] {
                    for sh in [0u32, 8, 16, 24] {
                        if o + 1 >= out.len() {
                            break;
                        }
                        let ch = ((w >> sh) & 0xff) as u8;
                        if ch == 0 {
                            continue;
                        }
                        out[o] = if ch < 0x80 { ch } else { b'?' };
                        o += 1;
                    }
                }
            }
            while o > 0 && (out[o - 1] == b' ' || out[o - 1] == 0) {
                o -= 1;
            }
            out[o] = 0;
        }
    }
    if !ok || out[0] == 0 {
        let fallback = b"unknown-cpu";
        out[..fallback.len()].copy_from_slice(fallback);
        out[fallback.len()] = 0;
    }
}

fn brand_str(brand: &[u8; 49]) -> &str {
    let mut n = 0usize;
    while n < brand.len() && brand[n] != 0 {
        n += 1;
    }
    /* Leading spaces common in brand strings. */
    let mut s = 0usize;
    while s < n && brand[s] == b' ' {
        s += 1;
    }
    core::str::from_utf8(&brand[s..n]).unwrap_or("unknown-cpu")
}

/// Print the boot welcome banner onto the log/console path.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_banner() {
    if pm_metal_util_ascii_render(b"METAL\0".as_ptr(), Some(log_row), core::ptr::null_mut()) != 0 {
        pm_metal_log_styled(
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT,
            b"METAL\0".as_ptr(),
        );
    }
    let mut brand = [0u8; 49];
    cpuid_brand(&mut brand);
    let mut line = LineBuf::new();
    let _ = write!(line, "Metal exp2 @ {}", brand_str(&brand));
    pm_metal_log_styled(
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT,
        line.as_cstr(),
    );
}
