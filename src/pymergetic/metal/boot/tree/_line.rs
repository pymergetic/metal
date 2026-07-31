//! Tree line helpers — ASCII prefixes + log styles.

use core::fmt::Write;

use pymergetic_metal_log::{pm_metal_log_style_t, pm_metal_log_styled};

pub struct LineBuf {
    buf: [u8; 192],
    pos: usize,
}

impl LineBuf {
    pub fn new() -> Self {
        Self {
            buf: [0; 192],
            pos: 0,
        }
    }

    pub fn clear(&mut self) {
        self.pos = 0;
        self.buf[0] = 0;
    }

    pub fn as_cstr(&mut self) -> *const u8 {
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

pub fn emit(style: pm_metal_log_style_t, line: &mut LineBuf) {
    unsafe {
        pm_metal_log_styled(style, line.as_cstr());
    }
}

pub fn emit_str(style: pm_metal_log_style_t, s: &str) {
    let mut line = LineBuf::new();
    let _ = write!(line, "{}", s);
    emit(style, &mut line);
}
