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
  INT32   argc,
  CHAR8 **argv
  )
{
  pm_metal_input_keyb_t  layout;
  CONST CHAR8           *name;
  CHAR8                  msg[48];

  if (argc < 2) {
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

  if (pm_metal_input_keyb_parse (argv[1], &layout) != 0) {
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

PM_METAL_SHELL_CMD (
  g_pm_metal_shell_cmd_keyb,
  "keyb",
  "keyb [us|gr]      keyboard layout (DOS KEYB)",
  InputKeybShellCmd
  );
