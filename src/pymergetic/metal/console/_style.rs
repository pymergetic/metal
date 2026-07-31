//! Semantic console styles -> ASCII SGR sequences (ESC[...m).

use crate::pm_metal_console_style_t;

/// SGR body digits after ESC[ and before m (no brackets). Empty => reset only.
pub fn sgr_code(style: pm_metal_console_style_t) -> &'static [u8] {
    match style {
        pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_DEFAULT => b"0",
        pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_DIM => b"2",
        pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_OK => b"32",
        pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_WARN => b"33",
        pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_FAIL => b"31",
        pm_metal_console_style_t::PM_METAL_CONSOLE_STYLE_ACCENT => b"36",
    }
}
