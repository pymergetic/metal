//! Minimal boot UI consumer — status stripe after gfx present.

use crate::compositor;
use crate::scanout;

/// Draw a bottom status bar with the bound scanout name and present once.
pub unsafe fn boot_stripe() -> i32 {
    if compositor::ready() == 0 {
        return 0;
    }
    let w = compositor::width();
    let h = compositor::height();
    if w <= 0 || h <= 0 {
        return 0;
    }
    let bar_h = 24;
    let y = if h > bar_h { h - bar_h } else { 0 };
    compositor::fill_rect(0, y, w, bar_h, compositor::rgb(0x10, 0x14, 0x20));
    let mut label = [0u8; 48];
    let prefix = b"scanout: ";
    let name = scanout::name().as_bytes();
    let mut i = 0usize;
    for &b in prefix {
        if i + 1 >= label.len() {
            break;
        }
        label[i] = b;
        i += 1;
    }
    for &b in name {
        if i + 1 >= label.len() {
            break;
        }
        label[i] = b;
        i += 1;
    }
    label[i] = 0;
    compositor::draw_text(
        8,
        y + 4,
        label.as_ptr(),
        compositor::rgb(0xc8, 0xd0, 0xe0),
        compositor::rgb(0, 0, 0),
        1,
    );
    if compositor::present() != 0 {
        return -1;
    }
    0
}
