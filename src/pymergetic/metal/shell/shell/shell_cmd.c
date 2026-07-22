/** @file
  Shell command registry + dispatch. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#define PM_METAL_SHELL_CMD_MAX  32u

STATIC CONST pm_metal_shell_cmd_t  *mCmds[PM_METAL_SHELL_CMD_MAX];
STATIC UINT32                       mCmdCount;

void
pm_metal_shell_cmd_register (
  CONST pm_metal_shell_cmd_t  *cmd
  )
{
  if (cmd == NULL || cmd->name == NULL || cmd->fn == NULL) {
    return;
  }

  if (mCmdCount >= PM_METAL_SHELL_CMD_MAX) {
    return;
  }

  mCmds[mCmdCount++] = cmd;
}

void
pm_metal_shell_cmd_help (
  VOID
  )
{
  UINT32  i;

  pm_metal_shell_out ("commands:");
  for (i = 0; i < mCmdCount; i++) {
    CHAR8  line[160];

    if (mCmds[i]->help != NULL) {
      AsciiSPrint (
        line,
        sizeof (line),
        "  %-12a %a",
        mCmds[i]->name,
        mCmds[i]->help
        );
    } else {
      AsciiSPrint (line, sizeof (line), "  %a", mCmds[i]->name);
    }

    pm_metal_shell_out (line);
  }
}

void
pm_metal_shell_cmd_dispatch (
  CONST CHAR8  *line
  )
{
  CHAR8        buf[120];
  CHAR8       *cmd;
  CHAR8       *arg;
  UINTN        i;

  if (line == NULL) {
    return;
  }

  while (*line == ' ') {
    line++;
  }

  if (*line == '\0') {
    return;
  }

  AsciiStrCpyS (buf, sizeof (buf), line);
  cmd = buf;
  arg = buf;
  while (*arg != '\0' && *arg != ' ') {
    arg++;
  }

  if (*arg == ' ') {
    *arg = '\0';
    arg++;
    while (*arg == ' ') {
      arg++;
    }
  }

  for (i = 0; i < mCmdCount; i++) {
    if (AsciiStrCmp (cmd, mCmds[i]->name) == 0) {
      mCmds[i]->fn (arg);
      return;
    }
  }

  {
    CHAR8  msg[96];

    AsciiSPrint (msg, sizeof (msg), "unknown: %a  (try help)", cmd);
    pm_metal_shell_out (msg);
  }
}

void
pm_metal_shell_cmds_install (
  VOID
  )
{
  mCmdCount = 0;
  pm_metal_shell_cmds_register_core ();
  pm_metal_shell_cmds_register_net ();
  pm_metal_shell_cmds_register_hwinfo ();
  pm_metal_shell_cmds_register_input ();
}
