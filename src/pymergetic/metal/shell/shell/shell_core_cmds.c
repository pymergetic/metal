/** @file
  Core shell commands (help, tabs, exit, …). (impl: efi|bios)
  `test` lives with boot — see boot_shell.c.
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/util/size.h>
#include <runtime/mem/mem.h>
#include <runtime/stack/stack.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

STATIC
VOID
CoreFmtBytes (
  CHAR8   *out,
  UINTN    cap,
  UINT64   bytes
  )
{
  if (pm_metal_util_size_format (out, (size_t)cap, bytes) < 0) {
    if (cap > 0) {
      out[0] = '?';
      if (cap > 1) {
        out[1] = '\0';
      }
    }
  }
}

STATIC
VOID
CoreMemCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  UINT64  phys;
  UINT64  arena;
  UINT64  outside;
  UINT64  map;
  UINT64  heap;
  UINT64  hole;
  UINT64  stacks;
  UINT64  map_other;
  UINT32  n_cpus;
  UINT32  pct_metal;
  UINT32  pct_hole;
  UINT32  pct_heap;
  CHAR8   pbuf[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   a[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   outb[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   s[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   one[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   oth[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   o[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   h[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  CHAR8   line[112];

  (VOID)argc;
  (VOID)argv;

  /*
   * system RAM
   *   +-- metal arena (low → high): stacks | map | hole | heap
   *   `-- other (UEFI / firmware / unclaimed)
   */
  phys   = (UINT64)pm_metal_mem_phys_bytes ();
  arena  = (UINT64)pm_metal_mem_arena_bytes ();
  map    = (UINT64)pm_metal_mem_map_bytes ();
  heap   = (UINT64)pm_metal_mem_heap_bytes ();
  hole   = (UINT64)pm_metal_mem_hole_bytes ();
  n_cpus = pm_metal_stack_n_cpus ();
  if (n_cpus == 0u) {
    n_cpus = pm_metal_mem_n_cpus ();
  }

  stacks = (UINT64)pm_metal_stack_bytes () * (UINT64)n_cpus;
  if (stacks > map) {
    stacks = map;
  }

  map_other = map - stacks;
  outside   = 0;
  if (phys > arena) {
    outside = phys - arena;
  }

  pct_metal = 0;
  pct_hole  = 0;
  pct_heap  = 0;
  if (phys != 0) {
    pct_metal = (UINT32)((arena * 100ull) / phys);
  }

  if (arena != 0) {
    pct_hole = (UINT32)((hole * 100ull) / arena);
    pct_heap = (UINT32)((heap * 100ull) / arena);
  }

  CoreFmtBytes (a, sizeof (a), arena);
  CoreFmtBytes (s, sizeof (s), stacks);
  CoreFmtBytes (one, sizeof (one), (UINT64)pm_metal_stack_bytes ());
  CoreFmtBytes (oth, sizeof (oth), map_other);
  CoreFmtBytes (o, sizeof (o), hole);
  CoreFmtBytes (h, sizeof (h), heap);

  if (phys != 0) {
    CoreFmtBytes (pbuf, sizeof (pbuf), phys);
    CoreFmtBytes (outb, sizeof (outb), outside);
    AsciiSPrint (line, sizeof (line), "mem: system %a", pbuf);
    pm_metal_shell_out (line);
    AsciiSPrint (
      line,
      sizeof (line),
      "  +-- metal   %a   (%u%%, claimed arena)",
      a,
      pct_metal
      );
    pm_metal_shell_out (line);
  } else {
    AsciiSPrint (line, sizeof (line), "mem: metal %a  (arena)", a);
    pm_metal_shell_out (line);
  }

  AsciiSPrint (
    line,
    sizeof (line),
    "  %a +-- stacks  %a   (%u x %a)",
    (phys != 0) ? "|  " : "",
    s,
    n_cpus,
    one
    );
  pm_metal_shell_out (line);
  AsciiSPrint (
    line,
    sizeof (line),
    "  %a +-- map     %a   (virtio/DMA/...)",
    (phys != 0) ? "|  " : "",
    oth
    );
  pm_metal_shell_out (line);
  AsciiSPrint (
    line,
    sizeof (line),
    "  %a +-- hole    %a   (%u%% of arena)",
    (phys != 0) ? "|  " : "",
    o,
    pct_hole
    );
  pm_metal_shell_out (line);
  AsciiSPrint (
    line,
    sizeof (line),
    "  %a `-- heap    %a   (%u%% of arena, TLSF)",
    (phys != 0) ? "|  " : "",
    h,
    pct_heap
    );
  pm_metal_shell_out (line);

  if (phys != 0) {
    AsciiSPrint (
      line,
      sizeof (line),
      "  `-- other   %a   (UEFI/firmware/reserved)",
      outb
      );
    pm_metal_shell_out (line);
  }
}

STATIC
CONST CHAR8 *
CorePsUiName (
  pm_metal_process_ui_kind_t  kind
  )
{
  switch (kind) {
    case PM_METAL_PROC_UI_TAB:
      return "tab";
    case PM_METAL_PROC_UI_FULLSCREEN:
      return "full";
    default:
      return "none";
  }
}

STATIC
VOID
CorePsCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  pm_metal_process_info_t  list[PM_METAL_PROCESS_MAX];
  UINT32                   n;
  UINT32                   i;
  CHAR8                    line[96];

  (VOID)argc;
  (VOID)argv;
  n = pm_metal_process_list (list, PM_METAL_PROCESS_MAX);
  if (n == 0) {
    pm_metal_shell_out ("ps: no processes");
    return;
  }

  AsciiSPrint (line, sizeof (line), "ps: %u", n);
  pm_metal_shell_out (line);
  for (i = 0; i < n; i++) {
    AsciiSPrint (
      line,
      sizeof (line),
      "  %u %a ui=%a tab=%u surf=%u%a",
      (UINT32)list[i].id,
      list[i].name,
      CorePsUiName ((pm_metal_process_ui_kind_t)list[i].ui_kind),
      (UINT32)list[i].tab,
      list[i].surface,
      (list[i].id == pm_metal_process_current ()) ? " *" : ""
      );
    pm_metal_shell_out (line);
  }
}

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
CoreDateCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  UINT64  ms;
  UINT32  tod;
  UINT32  hour;
  UINT32  min;
  UINT32  sec;
  INT32   tz;
  CHAR8   line[64];

  (VOID)argc;
  (VOID)argv;
  ms   = pm_metal_tz_local_ms ();
  tod  = (UINT32)((ms / 1000ull) % 86400ull);
  hour = tod / 3600u;
  min  = (tod % 3600u) / 60u;
  sec  = tod % 60u;
  tz   = pm_metal_tz_minutes ();
  AsciiSPrint (
    line,
    sizeof (line),
    "%02u:%02u:%02u %a (UTC%c%02d%02d)",
    hour,
    min,
    sec,
    pm_metal_tz_name (),
    (tz < 0) ? '-' : '+',
    (tz < 0) ? (-tz) / 60 : tz / 60,
    (tz < 0) ? (-tz) % 60 : tz % 60
    );
  pm_metal_shell_out (line);
}

STATIC
VOID
CoreTzCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  CHAR8  line[80];
  INT32  tz;

  if (argc >= 2 && argv[1] != NULL && argv[1][0] != '\0') {
    if (pm_metal_tz_set (argv[1]) != 0) {
      pm_metal_shell_out ("tz: unknown (use +HHMM or Europe/Berlin)");
      return;
    }
  }

  tz = pm_metal_tz_minutes ();
  AsciiSPrint (
    line,
    sizeof (line),
    "tz %a (UTC%c%02d%02d)",
    pm_metal_tz_name (),
    (tz < 0) ? '-' : '+',
    (tz < 0) ? (-tz) / 60 : tz / 60,
    (tz < 0) ? (-tz) % 60 : tz % 60
    );
  pm_metal_shell_out (line);
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

STATIC
VOID
CoreHistoryCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  UINT32  n;
  UINT32  i;
  CHAR8   entry[128];
  CHAR8   line[144];

  (VOID)argc;
  (VOID)argv;
  n = pm_metal_shell_history_count ();
  if (n == 0u) {
    pm_metal_shell_out ("history: (empty)");
    return;
  }

  for (i = 0; i < n; i++) {
    if (pm_metal_shell_history_get (i, entry, sizeof (entry)) != 0) {
      continue;
    }

    AsciiSPrint (line, sizeof (line), "%4u  %a", i + 1u, entry);
    pm_metal_shell_out (line);
  }
}

PM_METAL_SHELL_CMDS (g_pm_metal_shell_cmds_core) = {
  { "help", "this text", CoreHelpCmd },
  { "echo", "echo <text>       print text", CoreEchoCmd },
  { "date", "date              local wall clock", CoreDateCmd },
  { "tz", "tz [+HHMM|name]   get/set timezone", CoreTzCmd },
  { "history", "history           list command history", CoreHistoryCmd },
  { "run", "run <mod>         fullscreen in console (guest HID)", CoreRunCmd },
  { "tab", "tab <mod>         windowed in a new tab (guest HID)", CoreTabCmd },
  { "ps", "ps                list fake processes", CorePsCmd },
  { "mem", "mem               system RAM + arena layout", CoreMemCmd },
  { "tabs", "tabs              list tabs", CoreTabsCmd },
  { "use", "use <n>           activate tab index", CoreUseCmd },
  { "close", "close [n]         close tab n, or active/last guest", CoreCloseCmd },
  { "exit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "quit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "shutdown", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
};
PM_METAL_SHELL_CMDS_END (g_pm_metal_shell_cmds_core);
