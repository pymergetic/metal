/** @file
  Shell `keyb` / `ps2trace` commands — lives with input/keyb stack. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/dev/input/input.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void InputKeybShellCmd(int argc, char **argv)
{
  pm_metal_input_keyb_t layout;
  const char           *name;
  char                  msg[48];

  if (argc < 2) {
    name = pm_metal_input_keyb_name(pm_metal_input_keyb_get());
    snprintf(msg,
             sizeof(msg),
             "keyb: %s  (us|de; Ctrl+Alt+Home cycles; bottom-right cell)",
             (name != NULL) ? name : "?");
    pm_metal_shell_out(msg);
    return;
  }

  if (pm_metal_input_keyb_parse(argv[1], &layout) != 0) {
    pm_metal_shell_out("usage: keyb [us|de|gr]");
    return;
  }

  if (pm_metal_input_keyb_set(layout) != 0) {
    pm_metal_shell_out("keyb: failed");
    return;
  }

  name = pm_metal_input_keyb_name(layout);
  snprintf(msg, sizeof(msg), "keyb: %s", (name != NULL) ? name : "?");
  pm_metal_shell_out(msg);
}

PM_METAL_SHELL_CMD(
  g_pm_metal_shell_cmd_keyb,
  "keyb",
  "keyb [us|de]      PS/2 layout (default us; de QWERTZ, alias gr; Ctrl+Alt+Home cycles)",
  InputKeybShellCmd);

/*
 * Real-hardware scancode capture: QEMU's i8042 is a faithful, quirk-free
 * emulation, so a real keyboard/EC misbehaving (wrong byte, dropped
 * translate, AUX/kbd misrouting, ...) can't be reproduced there — this
 * prints the wire-level bytes the drain loop actually sees so a report
 * like "Backspace prints a digit" can be diagnosed from the real trace
 * instead of guessing from the static tables. See input.h /
 * pm_metal_input_ps2_trace_set.
 */
static void InputPs2TraceShellCmd(int argc, char **argv)
{
  int32_t on;

  if (argc < 2) {
    pm_metal_shell_out(pm_metal_input_ps2_trace_get() != 0 ? "ps2trace: on" : "ps2trace: off");
    return;
  }

  on = (strcmp(argv[1], "on") == 0) ? 1 : 0;
  if (!on && strcmp(argv[1], "off") != 0) {
    pm_metal_shell_out("usage: ps2trace [on|off]");
    return;
  }

  pm_metal_input_ps2_trace_set(on);
  pm_metal_shell_out(on ? "ps2trace: on  (watch the log - press the offending key)"
                        : "ps2trace: off");
}

PM_METAL_SHELL_CMD(
  g_pm_metal_shell_cmd_ps2trace,
  "ps2trace",
  "ps2trace [on|off] Log raw i8042 keyboard bytes (debug real-HW scancode mismatches)",
  InputPs2TraceShellCmd);
