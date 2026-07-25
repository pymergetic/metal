/** @file
  German QWERTZ keyboard layout — set-1 make -> ASCII. (impl: common)

  Lat15 / ISO-8859-15 bytes for umlauts — matches the VGA font.
  ß=0xDF ü=0xFC ö=0xF6 ä=0xE4 Ü=0xDC Ö=0xD6 Ä=0xC4 §=0xA7 °=0xB0 ´=0xB4
**/
#include <pymergetic/metal/dev/input/keyb_layout.h>

PM_METAL_KEYB_LAYOUT_BEGIN(g_pm_metal_keyb_layout_gr) = {
  .name    = "gr",
  .aliases = "de",
  .swap_yz = 1,
  .unshift = {
    [0x01] = 0x1b,       [0x02] = '1',        [0x03] = '2',        [0x04] = '3',        [0x05] = '4',
    [0x06] = '5',        [0x07] = '6',        [0x08] = '7',        [0x09] = '8',        [0x0A] = '9',
    [0x0B] = '0',        [0x0C] = (char)0xDF, [0x0D] = (char)0xB4, [0x0E] = 0x08,       [0x0F] = '\t',
    [0x10] = 'q',        [0x11] = 'w',        [0x12] = 'e',        [0x13] = 'r',        [0x14] = 't',
    [0x15] = 'z',        [0x16] = 'u',        [0x17] = 'i',        [0x18] = 'o',        [0x19] = 'p',
    [0x1A] = (char)0xFC, [0x1B] = '+',        [0x1C] = '\r',       [0x1E] = 'a',        [0x1F] = 's',
    [0x20] = 'd',        [0x21] = 'f',        [0x22] = 'g',        [0x23] = 'h',        [0x24] = 'j',
    [0x25] = 'k',        [0x26] = 'l',        [0x27] = (char)0xF6, [0x28] = (char)0xE4, [0x29] = '^',
    [0x2B] = '#',        [0x2C] = 'y',        [0x2D] = 'x',        [0x2E] = 'c',        [0x2F] = 'v',
    [0x30] = 'b',        [0x31] = 'n',        [0x32] = 'm',        [0x33] = ',',        [0x34] = '.',
    [0x35] = '-',        [0x39] = ' ',        [0x56] = '<',
    /* Numpad block — same physical keys/glyphs regardless of ASCII layout. */
    [0x37] = '*',        [0x47] = '7',        [0x48] = '8',        [0x49] = '9',        [0x4A] = '-',
    [0x4B] = '4',        [0x4C] = '5',        [0x4D] = '6',        [0x4E] = '+',        [0x4F] = '1',
    [0x50] = '2',        [0x51] = '3',        [0x52] = '0',        [0x53] = '.',
  },
  .shift = {
    [0x02] = '!', [0x03] = '"',        [0x04] = (char)0xA7, [0x05] = '$',        [0x06] = '%',
    [0x07] = '&', [0x08] = '/',        [0x09] = '(',        [0x0A] = ')',        [0x0B] = '=',
    [0x0C] = '?', [0x0D] = '`',        [0x0E] = 0x08,       [0x0F] = '\t',       [0x10] = 'Q',
    [0x11] = 'W', [0x12] = 'E',        [0x13] = 'R',        [0x14] = 'T',        [0x15] = 'Z',
    [0x16] = 'U', [0x17] = 'I',        [0x18] = 'O',        [0x19] = 'P',        [0x1A] = (char)0xDC,
    [0x1B] = '*', [0x1C] = '\r',       [0x1E] = 'A',        [0x1F] = 'S',        [0x20] = 'D',
    [0x21] = 'F', [0x22] = 'G',        [0x23] = 'H',        [0x24] = 'J',        [0x25] = 'K',
    [0x26] = 'L', [0x27] = (char)0xD6, [0x28] = (char)0xC4, [0x29] = (char)0xB0, [0x2B] = '\'',
    [0x2C] = 'Y', [0x2D] = 'X',        [0x2E] = 'C',        [0x2F] = 'V',        [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',        [0x33] = ';',        [0x34] = ':',        [0x35] = '_',
    [0x39] = ' ', [0x56] = '>',
  },
};
