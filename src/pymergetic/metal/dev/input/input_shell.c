/** @file
  Shell `keyb` command — lives with input/keyb stack. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/dev/input/input.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

STATIC
VOID
InputKeybShellCmd (
  CONST CHAR8  *arg
  )
{
  pm_metal_input_keyb_t  layout;
  CONST CHAR8           *name;
  CHAR8                  msg[48];

  if (arg == NULL || arg[0] == '\0') {
    name = pm_metal_input_keyb_name (pm_metal_input_keyb_get ());
    AsciiSPrint (
      msg,
      sizeof (msg),
      "keyb: %a  (keyb us | keyb gr)",
      (name != NULL) ? name : "?"
      );
    pm_metal_shell_out (msg);
    return;
  }

  if (pm_metal_input_keyb_parse (arg, &layout) != 0) {
    pm_metal_shell_out ("usage: keyb [us|gr]");
    return;
  }

  if (pm_metal_input_keyb_set (layout) != 0) {
    pm_metal_shell_out ("keyb: failed");
    return;
  }

  name = pm_metal_input_keyb_name (layout);
  AsciiSPrint (msg, sizeof (msg), "keyb: %a", (name != NULL) ? name : "?");
  pm_metal_shell_out (msg);
}

STATIC CONST pm_metal_shell_cmd_t  g_pm_metal_shell_cmd_keyb = {
  "keyb",
  "keyb [us|gr]      keyboard layout (DOS KEYB)",
  InputKeybShellCmd
};

void
pm_metal_shell_cmds_register_input (
  VOID
  )
{
  pm_metal_shell_cmd_register (&g_pm_metal_shell_cmd_keyb);
}
