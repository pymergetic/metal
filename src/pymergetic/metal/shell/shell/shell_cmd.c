/** @file
  Shell command registry + argv dispatch. (impl: efi|bios)

  Command tables live in linker section `.pm_metal_shell_cmds.*` (see
  PM_METAL_SHELL_CMD / PM_METAL_SHELL_CMDS). Bounds come from the port
  linker script (PROVIDE_HIDDEN __pm_metal_shell_cmds_{start,end}).
**/
#include <pymergetic/metal/shell/shell_cmd.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

extern CONST pm_metal_shell_cmd_table_t  __pm_metal_shell_cmds_start[];
extern CONST pm_metal_shell_cmd_table_t  __pm_metal_shell_cmds_end[];

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
    CHAR8   line[160];
    CHAR8   namepad[13];
    UINTN   n;
    UINTN   p;

    /* PrintLib has no %-width for %a — pad the name by hand. */
    n = AsciiStrLen (mCmds[i]->name);
    if (n > 12u) {
      n = 12u;
    }

    CopyMem (namepad, mCmds[i]->name, n);
    for (p = n; p < 12u; p++) {
      namepad[p] = ' ';
    }

    namepad[12] = '\0';
    if (mCmds[i]->help != NULL) {
      AsciiSPrint (
        line,
        sizeof (line),
        "  %a %a",
        namepad,
        mCmds[i]->help
        );
    } else {
      AsciiSPrint (line, sizeof (line), "  %a", namepad);
    }

    pm_metal_shell_out (line);
  }
}

STATIC
INT32
ShellIsSpace (
  CHAR8  c
  )
{
  return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? 1 : 0;
}

/**
 * Split @a line into argv in-place (NUL-terminates tokens in @a buf).
 * Supports "double quotes" and \\ \" escapes inside quotes.
 * Returns argc, or -1 on overflow / unmatched quote.
 */
STATIC
INT32
ShellSplitArgv (
  CHAR8   *buf,
  CHAR8  **argv,
  UINT32   argv_max
  )
{
  CHAR8   *p;
  CHAR8   *out;
  UINT32   argc;

  if (buf == NULL || argv == NULL || argv_max == 0) {
    return -1;
  }

  p    = buf;
  argc = 0;
  while (*p != '\0') {
    while (ShellIsSpace (*p)) {
      p++;
    }

    if (*p == '\0') {
      break;
    }

    if (argc >= argv_max) {
      return -1;
    }

    argv[argc++] = p;
    out          = p;

    if (*p == '"') {
      p++;
      while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
          p++;
          *out++ = *p++;
          continue;
        }

        *out++ = *p++;
      }

      if (*p != '"') {
        return -1;
      }

      p++;
      *out = '\0';
      continue;
    }

    while (*p != '\0' && !ShellIsSpace (*p)) {
      *out++ = *p++;
    }

    if (ShellIsSpace (*p)) {
      *out = '\0';
      p++;
    } else {
      *out = '\0';
    }
  }

  return (INT32)argc;
}

void
pm_metal_shell_cmd_dispatch (
  CONST CHAR8  *line
  )
{
  CHAR8   buf[160];
  CHAR8  *argv[PM_METAL_SHELL_ARGV_MAX];
  INT32   argc;
  UINTN   i;

  if (line == NULL) {
    return;
  }

  if (AsciiStrCpyS (buf, sizeof (buf), line) != RETURN_SUCCESS) {
    pm_metal_shell_out ("line too long");
    return;
  }

  argc = ShellSplitArgv (buf, argv, PM_METAL_SHELL_ARGV_MAX);
  if (argc < 0) {
    pm_metal_shell_out ("parse error (quote/too many args)");
    return;
  }

  if (argc == 0) {
    return;
  }

  for (i = 0; i < mCmdCount; i++) {
    if (AsciiStrCmp (argv[0], mCmds[i]->name) == 0) {
      mCmds[i]->fn (argc, argv);
      return;
    }
  }

  {
    CHAR8  msg[96];

    AsciiSPrint (msg, sizeof (msg), "unknown: %a  (try help)", argv[0]);
    pm_metal_shell_out (msg);
  }
}

void
pm_metal_shell_cmds_install (
  VOID
  )
{
  CONST pm_metal_shell_cmd_table_t  *t;

  mCmdCount = 0;
  for (t = __pm_metal_shell_cmds_start; t < __pm_metal_shell_cmds_end; t++) {
    UINT32  i;

    if (t->cmds == NULL || t->count == 0) {
      continue;
    }

    for (i = 0; i < t->count; i++) {
      pm_metal_shell_cmd_register (&t->cmds[i]);
    }
  }
}
