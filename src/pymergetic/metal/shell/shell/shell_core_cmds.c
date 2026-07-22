/** @file
  Core shell commands (help, tabs, exit, …). (impl: efi|bios)
  `test` lives with boot — see boot_shell.c.
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/ui/ui.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

STATIC
VOID
CoreHelpCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  (VOID)argc;
  (VOID)argv;
  pm_metal_shell_cmd_help ();
}

STATIC
VOID
CoreEchoCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  CHAR8  line[160];
  INT32  i;
  UINTN  off;
  UINTN  n;

  if (argc < 2) {
    pm_metal_shell_out ("");
    return;
  }

  off = 0;
  for (i = 1; i < argc; i++) {
    n = AsciiStrLen (argv[i]);
    if (i > 1) {
      if (off + 1 >= sizeof (line)) {
        break;
      }

      line[off++] = ' ';
    }

    if (off + n >= sizeof (line)) {
      n = sizeof (line) - 1u - off;
    }

    CopyMem (line + off, argv[i], n);
    off += n;
  }

  line[off] = '\0';
  pm_metal_shell_out (line);
}

STATIC
VOID
CoreRunCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out ("usage: run <mod>");
    return;
  }

  (VOID)pm_metal_shell_run (argv[1]);
}

STATIC
VOID
CoreTabCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out ("usage: tab <mod>");
    return;
  }

  (VOID)pm_metal_shell_tab (argv[1]);
}

STATIC
VOID
CoreTabsCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  UINT32  n;
  UINT32  i;
  UINT32  a;
  CHAR8   line[80];

  (VOID)argc;
  (VOID)argv;
  n = pm_metal_ui_tab_count ();
  a = pm_metal_ui_tab_active_index ();
  AsciiSPrint (line, sizeof (line), "tabs: %u  active: %u", n, a);
  pm_metal_shell_out (line);
  for (i = 0; i < n; i++) {
    AsciiSPrint (line, sizeof (line), "  %u%a", i, (i == a) ? " *" : "");
    pm_metal_shell_out (line);
  }
}

STATIC
VOID
CoreUseCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  UINTN  i;

  if (argc < 2) {
    pm_metal_shell_out ("usage: use <n>");
    return;
  }

  i = AsciiStrDecimalToUintn (argv[1]);
  if (pm_metal_ui_tab_activate_index ((UINT32)i) != 0) {
    pm_metal_shell_out ("use: bad index");
  } else {
    CHAR8  msg[40];

    AsciiSPrint (msg, sizeof (msg), "active tab %u", (UINT32)i);
    pm_metal_ui_set_status (msg);
    pm_metal_shell_mark_full ();
  }
}

STATIC
VOID
CoreCloseCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  UINT32  idx;
  UINT32  n;
  UINT32  a;

  n = pm_metal_ui_tab_count ();
  a = pm_metal_ui_tab_active_index ();
  if (argc >= 2) {
    idx = (UINT32)AsciiStrDecimalToUintn (argv[1]);
  } else if (a != 0) {
    idx = a;
  } else if (n > 1) {
    idx = n - 1;
  } else {
    pm_metal_shell_out ("close: no guest tab");
    return;
  }

  if (idx == 0) {
    pm_metal_shell_out ("close: cannot close console");
  } else if (pm_metal_ui_tab_activate_index (idx) != 0
             || pm_metal_ui_tab_close_active () != 0)
  {
    pm_metal_shell_out ("close: failed");
  } else {
    (VOID)pm_metal_ui_tab_activate_index (0);
    pm_metal_ui_set_status ("tab closed");
    pm_metal_shell_out ("tab closed");
    pm_metal_shell_mark_full ();
  }
}

STATIC
VOID
CoreExitCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  INT32  reboot;

  if (argc > 2) {
    pm_metal_shell_out ("usage: exit [-r]");
    return;
  }

  if (argc == 2 && AsciiStrCmp (argv[1], "-r") != 0) {
    pm_metal_shell_out ("exit: use -r to reboot");
    return;
  }

  reboot = (argc == 2) ? 1 : 0;
  pm_metal_shell_out (reboot ? "reboot requested" : "shutdown requested");
  pm_metal_shell_cmd_exit (reboot);
}

PM_METAL_SHELL_CMDS (g_pm_metal_shell_cmds_core) = {
  { "help", "this text", CoreHelpCmd },
  { "echo", "echo <text>       print text", CoreEchoCmd },
  { "run", "run <mod>         run wasm mod in current tab", CoreRunCmd },
  { "tab", "tab <mod>         run wasm mod in a new tab", CoreTabCmd },
  { "tabs", "tabs              list tabs", CoreTabsCmd },
  { "use", "use <n>           activate tab index", CoreUseCmd },
  { "close", "close [n]         close tab n, or active/last guest", CoreCloseCmd },
  { "exit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "quit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "shutdown", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
};
PM_METAL_SHELL_CMDS_END (g_pm_metal_shell_cmds_core);
