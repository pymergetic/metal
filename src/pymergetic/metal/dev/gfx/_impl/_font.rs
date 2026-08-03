//! VGA 8x16 font (Lat15) for draw_text.

pub const FONT_W: u32 = 8;
pub const FONT_H: u32 = 16;
const GLYPH_BYTES: usize = 16;
const N_GLYPHS: usize = 256;

static GLYPHS: &[u8; N_GLYPHS * GLYPH_BYTES] =
    include_bytes!("font_vga8x16.bin");

pub fn glyph_row(ch: u8, row: usize) -> u8 {
    if row >= GLYPH_BYTES {
        return 0;
    }
    GLYPHS[(ch as usize) * GLYPH_BYTES + row]
}
