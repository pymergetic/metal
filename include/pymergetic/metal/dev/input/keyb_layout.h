/*
 * Metal keyboard layouts — registered per-language descriptors.
 *
 * Each language lives in its own file under
 * src/pymergetic/metal/dev/input/keyb_layout/ and contributes exactly one
 * pm_metal_keyb_layout_t via PM_METAL_KEYB_LAYOUT_BEGIN (linker section
 * `.pm_metal_keyb_layouts.*` — same self-registration idiom as
 * PM_METAL_SHELL_CMD / PM_METAL_PY_BIND). Adding a language is "drop a new
 * keyb_layout_<id>.c file in that folder" — never touches keyb.c, this
 * header, or any linker script.
 *
 * impl: common — src/pymergetic/metal/dev/input/keyb.c (registry walk +
 * public pm_metal_input_keyb_* API), src/pymergetic/metal/dev/input/
 * keyb_layout/keyb_layout_*.c (one descriptor per language)
 */
#ifndef PYMERGETIC_METAL_DEV_INPUT_KEYB_LAYOUT_H_
#define PYMERGETIC_METAL_DEV_INPUT_KEYB_LAYOUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * One DOS-KEYB-style set-1 -> ASCII layout. `unshift`/`shift` are indexed by
 * the set-1 make code (low 7 bits); 0 = no glyph. `name` is the id `keyb
 * <name>` sets and `keyb` prints; `aliases` is an optional space-separated
 * list of extra accepted ids (e.g. GR also accepts "de"), or NULL.
 */
typedef struct pm_metal_keyb_layout {
  const char *name;
  const char *aliases;
  const char  unshift[0x80];
  const char  shift[0x80];
  /*
   * Layout-specific HID quirk: some layouts (e.g. QWERTZ) swap the Y/Z
   * scancodes at the keycap/ASCII layer; HID stays positional for
   * everything else (guests keep stable keycodes across `keyb` changes).
   */
  int32_t swap_yz;
} pm_metal_keyb_layout_t;

/**
 * Declare one auto-registered layout. Follow with `= { .name = ..., ... };`
 * — the large `unshift`/`shift` designated-initializer tables are written
 * directly by the layout file, not threaded through macro arguments.
 * `var` must be a unique static identifier in the translation unit.
 */
#define PM_METAL_KEYB_LAYOUT_BEGIN(var)   \
  static const pm_metal_keyb_layout_t var \
    __attribute__((used, section(".pm_metal_keyb_layouts.1"), aligned(16)))

/** Section bounds, walked by keyb.c; never dereference outside that file. */
extern const pm_metal_keyb_layout_t __pm_metal_keyb_layouts_start[];
extern const pm_metal_keyb_layout_t __pm_metal_keyb_layouts_end[];

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_INPUT_KEYB_LAYOUT_H_ */
