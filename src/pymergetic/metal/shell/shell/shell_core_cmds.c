/** @file
  Core shell commands (help, tabs, exit, …). (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/boot/boot.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

STATIC
VOID
CoreHelpCmd (
  CONST CHAR8  *arg
  )
{
  (VOID)arg;
  pm_metal_shell_cmd_help ();
}

STATIC
VOID
CoreEchoCmd (
  CONST CHAR8  *arg
  )
{
  pm_metal_shell_out (arg);
}

STATIC
VOID
CoreTestCmd (
  CONST CHAR8  *arg
  )
{
  (VOID)arg;
  if (pm_metal_boot_run_tests () != 0) {
    pm_metal_shell_out ("test: FAILED");
  } else {
    pm_metal_shell_out ("test: ok");
  }
}

STATIC
VOID
CoreRunCmd (
  CONST CHAR8  *arg
  )
{
  (VOID)pm_metal_shell_run (arg);
}

STATIC
VOID
CoreTabCmd (
  CONST CHAR8  *arg
  )
{
  (VOID)pm_metal_shell_tab (arg);
}

STATIC
VOID
CoreTabsCmd (
  CONST CHAR8  *arg
  )
{
  UINT32  n;
  UINT32  i;
  UINT32  a;
  CHAR8   line[80];

  (VOID)arg;
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
  CONST CHAR8  *arg
  )
{
  UINTN  i;

  i = AsciiStrDecimalToUintn (arg);
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
  CONST CHAR8  *arg
  )
{
  UINT32  idx;
  UINT32  n;
  UINT32  a;

  n = pm_metal_ui_tab_count ();
  a = pm_metal_ui_tab_active_index ();
  if (arg != NULL && arg[0] != '\0') {
    idx = (UINT32)AsciiStrDecimalToUintn (arg);
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
  CONST CHAR8  *arg
  )
{
  INT32  reboot;

  if (arg != NULL && arg[0] != '\0' && AsciiStrCmp (arg, "-r") != 0) {
    pm_metal_shell_out ("exit: use -r to reboot");
    return;
  }

  reboot = (arg != NULL && AsciiStrCmp (arg, "-r") == 0) ? 1 : 0;
  pm_metal_shell_out (reboot ? "reboot requested" : "shutdown requested");
  pm_metal_shell_cmd_exit (reboot);
}

STATIC CONST pm_metal_shell_cmd_t  g_pm_metal_shell_cmds_core[] = {
  { "help", "this text", CoreHelpCmd },
  { "echo", "echo <text>       print text", CoreEchoCmd },
  { "run", "run <mod>         run wasm mod in current tab", CoreRunCmd },
  { "tab", "tab <mod>         run wasm mod in a new tab", CoreTabCmd },
  { "tabs", "tabs              list tabs", CoreTabsCmd },
  { "use", "use <n>           activate tab index", CoreUseCmd },
  { "close", "close [n]         close tab n, or active/last guest", CoreCloseCmd },
  { "test", "test              run bring-up proof suite", CoreTestCmd },
  { "exit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "quit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "shutdown", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
};

void
pm_metal_shell_cmds_register_core (
  VOID
  )
{
  UINTN  i;

  for (i = 0; i < sizeof (g_pm_metal_shell_cmds_core) / sizeof (g_pm_metal_shell_cmds_core[0]); i++) {
    pm_metal_shell_cmd_register (&g_pm_metal_shell_cmds_core[i]);
  }
}
