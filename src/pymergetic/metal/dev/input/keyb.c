/** @file
  DOS KEYB-style set-1 → ASCII layouts (US / GR). (impl: common)
**/
#include <pymergetic/metal/dev/input/input.h>

#include <Uefi.h>
#include <Library/BaseLib.h>

STATIC pm_metal_input_keyb_t  mKeyb = PM_METAL_INPUT_KEYB_US;

/* US QWERTY — set-1 make → ASCII */
STATIC CONST CHAR8  mUsUnshift[0x80] = {
  [0x01] = 0x1b,
  [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
  [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
  [0x0C] = '-', [0x0D] = '=', [0x0E] = 0x08, [0x0F] = '\t',
  [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
  [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
  [0x1A] = '[', [0x1B] = ']', [0x1C] = '\r',
  [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
  [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
  [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
  [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
  [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
  [0x39] = ' ',
};

STATIC CONST CHAR8  mUsShift[0x80] = {
  [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
  [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
  [0x0C] = '_', [0x0D] = '+', [0x0E] = 0x08, [0x0F] = '\t',
  [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
  [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
  [0x1A] = '{', [0x1B] = '}', [0x1C] = '\r',
  [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
  [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
  [0x28] = '"', [0x29] = '~', [0x2B] = '|',
  [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
  [0x31] = 'N', [0x32] = 'M', [0x33] = ',', [0x34] = '.', [0x35] = '?',
  [0x39] = ' ',
};

/*
 * German QWERTZ (Lat15 / ISO-8859-15 bytes for umlauts — matches VGA font).
 * ß=0xDF ü=0xFC ö=0xF6 ä=0xE4 Ü=0xDC Ö=0xD6 Ä=0xC4 §=0xA7 °=0xB0 ´=0xB4
 */
STATIC CONST CHAR8  mGrUnshift[0x80] = {
  [0x01] = 0x1b,
  [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
  [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
  [0x0C] = (CHAR8)0xDF, [0x0D] = (CHAR8)0xB4, [0x0E] = 0x08, [0x0F] = '\t',
  [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
  [0x15] = 'z', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
  [0x1A] = (CHAR8)0xFC, [0x1B] = '+', [0x1C] = '\r',
  [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
  [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
  [0x27] = (CHAR8)0xF6, [0x28] = (CHAR8)0xE4, [0x29] = '^', [0x2B] = '#',
  [0x2C] = 'y', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
  [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '-',
  [0x39] = ' ',
  [0x56] = '<',
};

STATIC CONST CHAR8  mGrShift[0x80] = {
  [0x02] = '!', [0x03] = '"', [0x04] = (CHAR8)0xA7, [0x05] = '$', [0x06] = '%',
  [0x07] = '&', [0x08] = '/', [0x09] = '(', [0x0A] = ')', [0x0B] = '=',
  [0x0C] = '?', [0x0D] = '`', [0x0E] = 0x08, [0x0F] = '\t',
  [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
  [0x15] = 'Z', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
  [0x1A] = (CHAR8)0xDC, [0x1B] = '*', [0x1C] = '\r',
  [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
  [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
  [0x27] = (CHAR8)0xD6, [0x28] = (CHAR8)0xC4, [0x29] = (CHAR8)0xB0,
  [0x2B] = '\'',
  [0x2C] = 'Y', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
  [0x31] = 'N', [0x32] = 'M', [0x33] = ';', [0x34] = ':', [0x35] = '_',
  [0x39] = ' ',
  [0x56] = '>',
};

int
pm_metal_input_keyb_set (
  pm_metal_input_keyb_t  layout
  )
{
  if (layout != PM_METAL_INPUT_KEYB_US
      && layout != PM_METAL_INPUT_KEYB_GR)
  {
    return -1;
  }

  mKeyb = layout;
  return 0;
}

pm_metal_input_keyb_t
pm_metal_input_keyb_get (
  VOID
  )
{
  return mKeyb;
}

CONST CHAR8 *
pm_metal_input_keyb_name (
  pm_metal_input_keyb_t  layout
  )
{
  switch (layout) {
    case PM_METAL_INPUT_KEYB_US:
      return "us";
    case PM_METAL_INPUT_KEYB_GR:
      return "gr";
    default:
      return NULL;
  }
}

int
pm_metal_input_keyb_parse (
  CONST CHAR8             *id,
  pm_metal_input_keyb_t   *out
  )
{
  if (id == NULL || out == NULL || id[0] == '\0') {
    return -1;
  }

  if (AsciiStrCmp (id, "us") == 0 || AsciiStrCmp (id, "US") == 0) {
    *out = PM_METAL_INPUT_KEYB_US;
    return 0;
  }

  /* DOS KEYB GR; also accept de */
  if (AsciiStrCmp (id, "gr") == 0 || AsciiStrCmp (id, "GR") == 0
      || AsciiStrCmp (id, "de") == 0 || AsciiStrCmp (id, "DE") == 0)
  {
    *out = PM_METAL_INPUT_KEYB_GR;
    return 0;
  }

  return -1;
}

CHAR8
pm_metal_input_keyb_ascii (
  UINT8  set1_make,
  INT32  shift
  )
{
  UINT8         sc;
  CONST CHAR8  *map;

  sc = (UINT8)(set1_make & 0x7Fu);
  if (mKeyb == PM_METAL_INPUT_KEYB_GR) {
    map = (shift != 0) ? mGrShift : mGrUnshift;
  } else {
    map = (shift != 0) ? mUsShift : mUsUnshift;
  }

  return map[sc];
}

pm_metal_keycode_t
pm_metal_input_keyb_hid (
  UINT8  set1_make,
  INT32  ext
  )
{
  UINT8  code;

  code = (UINT8)(set1_make & 0x7Fu);
  if (ext != 0) {
    switch (code) {
      case 0x4Bu:
        return PM_METAL_KEY_LEFT;
      case 0x4Du:
        return PM_METAL_KEY_RIGHT;
      case 0x48u:
        return PM_METAL_KEY_UP;
      case 0x50u:
        return PM_METAL_KEY_DOWN;
      case 0x49u:
        return PM_METAL_KEY_PAGEUP;
      case 0x51u:
        return PM_METAL_KEY_PAGEDOWN;
      case 0x1Du:
        return PM_METAL_KEY_RCTRL;
      case 0x38u:
        return PM_METAL_KEY_RALT;
      default:
        return PM_METAL_KEY_NONE;
    }
  }

  switch (code) {
    case 0x01u:
      return PM_METAL_KEY_ESCAPE;
    case 0x0Fu:
      return PM_METAL_KEY_TAB;
    case 0x0Eu:
      return PM_METAL_KEY_BACKSPACE;
    case 0x1Cu:
      return PM_METAL_KEY_ENTER;
    case 0x39u:
      return PM_METAL_KEY_SPACE;
    case 0x2Au:
      return PM_METAL_KEY_LSHIFT;
    case 0x36u:
      return PM_METAL_KEY_RSHIFT;
    case 0x1Du:
      return PM_METAL_KEY_LCTRL;
    case 0x38u:
      return PM_METAL_KEY_LALT;
    case 0x1Eu:
      return PM_METAL_KEY_A;
    case 0x30u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('b' - 'a'));
    case 0x2Eu:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('c' - 'a'));
    case 0x20u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('d' - 'a'));
    case 0x12u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('e' - 'a'));
    case 0x21u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('f' - 'a'));
    case 0x22u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('g' - 'a'));
    case 0x23u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('h' - 'a'));
    case 0x17u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('i' - 'a'));
    case 0x24u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('j' - 'a'));
    case 0x25u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('k' - 'a'));
    case 0x26u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('l' - 'a'));
    case 0x32u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('m' - 'a'));
    case 0x31u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('n' - 'a'));
    case 0x18u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('o' - 'a'));
    case 0x19u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('p' - 'a'));
    case 0x10u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('q' - 'a'));
    case 0x13u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('r' - 'a'));
    case 0x1Fu:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('s' - 'a'));
    case 0x14u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('t' - 'a'));
    case 0x16u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('u' - 'a'));
    case 0x2Fu:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('v' - 'a'));
    case 0x11u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('w' - 'a'));
    case 0x2Du:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('x' - 'a'));
    case 0x15u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('y' - 'a'));
    case 0x2Cu:
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('z' - 'a'));
    case 0x02u:
      return PM_METAL_KEY_1;
    case 0x03u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 1);
    case 0x04u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 2);
    case 0x05u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 3);
    case 0x06u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 4);
    case 0x07u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 5);
    case 0x08u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 6);
    case 0x09u:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 7);
    case 0x0Au:
      return (pm_metal_keycode_t)(PM_METAL_KEY_1 + 8);
    case 0x0Bu:
      return PM_METAL_KEY_0;
    default:
      break;
  }

  if (code >= 0x3Bu && code <= 0x44u) {
    return (pm_metal_keycode_t)(PM_METAL_KEY_F1 + (code - 0x3Bu));
  }

  return PM_METAL_KEY_NONE;
}
