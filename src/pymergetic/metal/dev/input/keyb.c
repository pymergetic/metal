/** @file
  DOS KEYB-style set-1 → ASCII layouts — registry walk. (impl: common)

  The actual per-language tables live under dev/input/keyb_layout/'s *.c
  files and self-register into the `.pm_metal_keyb_layouts.*` linker section (see
  keyb_layout.h) — this file only knows how to walk that section and drive
  the current selection. Adding a language never touches this file.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/input/keyb_layout.h>

static const pm_metal_keyb_layout_t *mCurrent;

static uint32_t KeybLayoutCount(void)
{
  return (uint32_t)(__pm_metal_keyb_layouts_end - __pm_metal_keyb_layouts_start);
}

static char AsciiLower(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive, NUL-terminated-vs-bounded compare (no libc strcasecmp
 * in this freestanding build). */
static int AsciiEqICaseBounded(const char *id, const char *tok, size_t tok_len)
{
  size_t i;

  for (i = 0; i < tok_len; i++) {
    if (id[i] == '\0' || AsciiLower(id[i]) != AsciiLower(tok[i])) {
      return 0;
    }
  }

  return id[tok_len] == '\0';
}

/* Case-insensitive match of `id` against `name` or a space-separated token
 * in `aliases` (NULL-safe). */
static int KeybLayoutMatches(const pm_metal_keyb_layout_t *layout, const char *id)
{
  const char *a;
  const char *tok;
  size_t      len;

  if (AsciiEqICaseBounded(id, layout->name, strlen(layout->name))) {
    return 1;
  }

  a = layout->aliases;
  while (a != NULL && *a != '\0') {
    tok = a;
    while (*a != '\0' && *a != ' ') {
      a++;
    }

    len = (size_t)(a - tok);
    if (AsciiEqICaseBounded(id, tok, len)) {
      return 1;
    }

    while (*a == ' ') {
      a++;
    }
  }

  return 0;
}

/* Lazily default to the first registered "us" (falls back to slot 0 if a
 * build ever ships without one, so we never dereference NULL). */
static const pm_metal_keyb_layout_t *KeybLayoutDefault(void)
{
  uint32_t i;
  uint32_t n;

  n = KeybLayoutCount();
  for (i = 0; i < n; i++) {
    if (strcmp(__pm_metal_keyb_layouts_start[i].name, "us") == 0) {
      return &__pm_metal_keyb_layouts_start[i];
    }
  }

  return (n != 0) ? &__pm_metal_keyb_layouts_start[0] : NULL;
}

static const pm_metal_keyb_layout_t *KeybLayoutCurrent(void)
{
  if (mCurrent == NULL) {
    mCurrent = KeybLayoutDefault();
  }

  return mCurrent;
}

int pm_metal_input_keyb_set(pm_metal_input_keyb_t layout)
{
  if (layout >= KeybLayoutCount()) {
    return -1;
  }

  mCurrent = &__pm_metal_keyb_layouts_start[layout];
  return 0;
}

pm_metal_input_keyb_t pm_metal_input_keyb_get(void)
{
  const pm_metal_keyb_layout_t *cur;

  cur = KeybLayoutCurrent();
  return (cur != NULL) ? (pm_metal_input_keyb_t)(cur - __pm_metal_keyb_layouts_start) : 0;
}

const char *pm_metal_input_keyb_name(pm_metal_input_keyb_t layout)
{
  if (layout >= KeybLayoutCount()) {
    return NULL;
  }

  return __pm_metal_keyb_layouts_start[layout].name;
}

int pm_metal_input_keyb_parse(const char *id, pm_metal_input_keyb_t *out)
{
  uint32_t i;
  uint32_t n;

  if (id == NULL || out == NULL || id[0] == '\0') {
    return -1;
  }

  n = KeybLayoutCount();
  for (i = 0; i < n; i++) {
    if (KeybLayoutMatches(&__pm_metal_keyb_layouts_start[i], id)) {
      *out = i;
      return 0;
    }
  }

  return -1;
}

char pm_metal_input_keyb_ascii(uint8_t set1_make, int32_t shift)
{
  uint8_t                       sc;
  const pm_metal_keyb_layout_t *cur;

  sc  = (uint8_t)(set1_make & 0x7Fu);
  cur = KeybLayoutCurrent();
  if (cur == NULL) {
    return 0;
  }

  return (shift != 0) ? cur->shift[sc] : cur->unshift[sc];
}

pm_metal_keycode_t pm_metal_input_keyb_hid(uint8_t set1_make, int32_t ext)
{
  uint8_t                       code;
  const pm_metal_keyb_layout_t *cur;

  code = (uint8_t)(set1_make & 0x7Fu);
  if (ext != 0) {
    /* E0-prefixed: dedicated nav cluster + right-hand mods + Menu/GUI. */
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
    case 0x47u:
      return PM_METAL_KEY_HOME;
    case 0x4Fu:
      return PM_METAL_KEY_END;
    case 0x52u:
      return PM_METAL_KEY_INSERT;
    case 0x53u:
      return PM_METAL_KEY_DELETE;
    case 0x1Cu:
      return PM_METAL_KEY_KP_ENTER;
    case 0x35u:
      return PM_METAL_KEY_KP_SLASH;
    case 0x1Du:
      return PM_METAL_KEY_RCTRL;
    case 0x38u:
      return PM_METAL_KEY_RALT;
    case 0x5Bu:
      return PM_METAL_KEY_LGUI;
    case 0x5Cu:
      return PM_METAL_KEY_RGUI;
    case 0x5Du:
      return PM_METAL_KEY_MENU;
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
  case 0x0Cu:
    return PM_METAL_KEY_MINUS;
  case 0x0Du:
    return PM_METAL_KEY_EQUAL;
  case 0x1Au:
    return PM_METAL_KEY_LEFTBRACE;
  case 0x1Bu:
    return PM_METAL_KEY_RIGHTBRACE;
  case 0x2Bu:
    return PM_METAL_KEY_BACKSLASH;
  case 0x27u:
    return PM_METAL_KEY_SEMICOLON;
  case 0x28u:
    return PM_METAL_KEY_APOSTROPHE;
  case 0x29u:
    return PM_METAL_KEY_GRAVE;
  case 0x33u:
    return PM_METAL_KEY_COMMA;
  case 0x34u:
    return PM_METAL_KEY_PERIOD;
  case 0x35u:
    return PM_METAL_KEY_SLASH;
  case 0x3Au:
    return PM_METAL_KEY_CAPSLOCK;
  case 0x45u:
    return PM_METAL_KEY_NUMLOCK;
  case 0x46u:
    return PM_METAL_KEY_SCROLLLOCK;
  case 0x56u:
    return PM_METAL_KEY_NONUSBSLASH;
  case 0x57u:
    return PM_METAL_KEY_F11;
  case 0x58u:
    return PM_METAL_KEY_F12;
  case 0x37u:
    return PM_METAL_KEY_KP_ASTERISK;
  case 0x47u:
    return PM_METAL_KEY_KP_7;
  case 0x48u:
    return PM_METAL_KEY_KP_8;
  case 0x49u:
    return PM_METAL_KEY_KP_9;
  case 0x4Au:
    return PM_METAL_KEY_KP_MINUS;
  case 0x4Bu:
    return PM_METAL_KEY_KP_4;
  case 0x4Cu:
    return PM_METAL_KEY_KP_5;
  case 0x4Du:
    return PM_METAL_KEY_KP_6;
  case 0x4Eu:
    return PM_METAL_KEY_KP_PLUS;
  case 0x4Fu:
    return PM_METAL_KEY_KP_1;
  case 0x50u:
    return PM_METAL_KEY_KP_2;
  case 0x51u:
    return PM_METAL_KEY_KP_3;
  case 0x52u:
    return PM_METAL_KEY_KP_0;
  case 0x53u:
    return PM_METAL_KEY_KP_PERIOD;
  /*
     * Letter HID follows keycap / DOS KEYB layout. US: 0x15=Y 0x2C=Z.
     * GR QWERTZ swaps those scancodes (Doom "press Y" vs keycap Y).
     */
  case 0x15u:
    cur = KeybLayoutCurrent();
    if (cur != NULL && cur->swap_yz != 0) {
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('z' - 'a'));
    }

    return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('y' - 'a'));
  case 0x2Cu:
    cur = KeybLayoutCurrent();
    if (cur != NULL && cur->swap_yz != 0) {
      return (pm_metal_keycode_t)(PM_METAL_KEY_A + ('y' - 'a'));
    }

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
