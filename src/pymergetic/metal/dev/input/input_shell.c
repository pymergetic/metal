/** @file
  Shell `keyb` command — lives with input/keyb stack. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/dev/input/input.h>

#include <stddef.h>
#include <stdio.h>

static void InputKeybShellCmd(int argc, char **argv)
{
  pm_metal_input_keyb_t layout;
  const char           *name;
  char                  msg[48];

  if (argc < 2) {
    name = pm_metal_input_keyb_name(pm_metal_input_keyb_get());
    snprintf(msg,
             sizeof(msg),
             "keyb: %s  (us|gr - post-EBS PS/2; shell + Doom HID)",
             (name != NULL) ? name : "?");
    pm_metal_shell_out(msg);
    return;
  }

  if (pm_metal_input_keyb_parse(argv[1], &layout) != 0) {
    pm_metal_shell_out("usage: keyb [us|gr|de]");
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

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_keyb,
                   "keyb",
                   "keyb [us|gr]      PS/2 layout (default us; de=gr QWERTZ)",
                   InputKeybShellCmd);
