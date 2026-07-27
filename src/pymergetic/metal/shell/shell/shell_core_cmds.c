/** @file
  Core shell commands (help, tabs, exit, …). (impl: efi|bios)
  `test` lives with boot — see boot_shell.c.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/util/size.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/py/py.h>
#include <runtime/run/run.h>
#include <runtime/stack/stack.h>

static void CoreFmtBytes(char *out, uintptr_t cap, uint64_t bytes)
{
  if (pm_metal_util_size_format(out, (size_t)cap, bytes) < 0) {
    if (cap > 0) {
      out[0] = '?';
      if (cap > 1) {
        out[1] = '\0';
      }
    }
  }
}

/** One decimal percent: "0.1%", "31.4%". tenths = round(part*1000/whole). */
static void CoreFmtPct1(char *out, uintptr_t cap, uint64_t part, uint64_t whole)
{
  uint64_t tenths;

  if (cap == 0) {
    return;
  }

  if (whole == 0) {
    snprintf(out, cap, "%s", "?%");
    return;
  }

  tenths = (part * 1000ull + whole / 2ull) / whole;
  snprintf(out, cap, "%llu.%llu%%", tenths / 10ull, tenths % 10ull);
}

static void CoreMemCmd(int32_t argc, char **argv)
{
  uint64_t    phys;
  uint64_t    arena;
  uint64_t    outside;
  uint64_t    map;
  uint64_t    heap;
  uint64_t    hole;
  uint64_t    stacks;
  uint64_t    map_other;
  uint64_t    heap_used;
  uint64_t    heap_free;
  size_t      pool_used;
  size_t      pool_free;
  uint32_t    n_cpus;
  char        pbuf[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        a[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        outb[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        s[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        one[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        oth[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        o[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        h[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        hu[PM_METAL_UTIL_SIZE_FORMAT_MAX];
  char        pct_metal[8];
  char        pct_stacks[8];
  char        pct_map[8];
  char        pct_hole[8];
  char        pct_heap[8];
  char        pct_heap_free[8];
  char        line[160];
  const char *branch;

  (void)argc;
  (void)argv;

  /*
   * system RAM
   *   +-- metal arena (low → high): stacks | map | hole | heap
   *   `-- other (UEFI / firmware / unclaimed)
   *
   * map/heap sizes = carved spans. hole = grow room for either.
   * heap also reports free-within-TLSF (left in the carved pool).
   */
  phys   = (uint64_t)pm_metal_mem_phys_bytes();
  arena  = (uint64_t)pm_metal_mem_arena_bytes();
  map    = (uint64_t)pm_metal_mem_map_bytes();
  heap   = (uint64_t)pm_metal_mem_heap_bytes();
  hole   = (uint64_t)pm_metal_mem_hole_bytes();
  n_cpus = pm_metal_stack_n_cpus();
  if (n_cpus == 0u) {
    n_cpus = pm_metal_mem_n_cpus();
  }

  stacks = (uint64_t)pm_metal_stack_bytes() * (uint64_t)n_cpus;
  if (stacks > map) {
    stacks = map;
  }

  map_other = map - stacks;
  outside   = 0;
  if (phys > arena) {
    outside = phys - arena;
  }

  pool_used = 0;
  pool_free = 0;
  pm_metal_mem_heap_pool_bytes(&pool_used, &pool_free);
  heap_used = (uint64_t)pool_used;
  heap_free = (uint64_t)pool_free;

  CoreFmtPct1(pct_metal, sizeof(pct_metal), arena, phys);
  CoreFmtPct1(pct_stacks, sizeof(pct_stacks), stacks, arena);
  CoreFmtPct1(pct_map, sizeof(pct_map), map_other, arena);
  CoreFmtPct1(pct_hole, sizeof(pct_hole), hole, arena);
  CoreFmtPct1(pct_heap, sizeof(pct_heap), heap, arena);
  CoreFmtPct1(pct_heap_free, sizeof(pct_heap_free), heap_free, heap_used + heap_free);

  CoreFmtBytes(a, sizeof(a), arena);
  CoreFmtBytes(s, sizeof(s), stacks);
  CoreFmtBytes(one, sizeof(one), (uint64_t)pm_metal_stack_bytes());
  CoreFmtBytes(oth, sizeof(oth), map_other);
  CoreFmtBytes(o, sizeof(o), hole);
  CoreFmtBytes(h, sizeof(h), heap);
  CoreFmtBytes(hu, sizeof(hu), heap_used);

  branch = (phys != 0) ? "|  " : "";

  if (phys != 0) {
    CoreFmtBytes(pbuf, sizeof(pbuf), phys);
    CoreFmtBytes(outb, sizeof(outb), outside);
    snprintf(line, sizeof(line), "mem: system %s", pbuf);
    pm_metal_shell_out(line);
    snprintf(line, sizeof(line), "  +-- metal   %s   (%s, claimed arena)", a, pct_metal);
    pm_metal_shell_out(line);
  } else {
    snprintf(line, sizeof(line), "mem: metal %s  (arena)", a);
    pm_metal_shell_out(line);
  }

  snprintf(line,
           sizeof(line),
           "  %s +-- stacks  %s   (%s of arena, %u x %s)",
           branch,
           s,
           pct_stacks,
           n_cpus,
           one);
  pm_metal_shell_out(line);
  snprintf(line,
           sizeof(line),
           "  %s +-- map     %s   (%s of arena; committed, virtio/DMA/...)",
           branch,
           oth,
           pct_map);
  pm_metal_shell_out(line);
  {
    /* µPy blob (PM_METAL_PY_BLOB_BYTES, py.c) is one map carve among
     * several (stacks, virtio/DMA, ...) — break it out here instead of
     * leaving it invisible inside map_other's aggregate. */
    uint64_t py_bytes = (uint64_t)pm_metal_py_blob_bytes();
    char     pyb[PM_METAL_UTIL_SIZE_FORMAT_MAX];
    char     pct_py[8];

    CoreFmtBytes(pyb, sizeof(pyb), py_bytes);
    CoreFmtPct1(pct_py, sizeof(pct_py), py_bytes, map_other);
    snprintf(line,
             sizeof(line),
             "  %s |   +-- py      %s   (%s of map, shared context)",
             branch,
             pyb,
             pct_py);
    pm_metal_shell_out(line);
  }
  {
    /* Isolated contexts (py -x / pm_metal_py_run_*_isolated, py_ctx.c) are
     * each their own separate MAP carve, on top of the one shared blob
     * broken out above — surface the aggregate here too, or task-local
     * heaps would otherwise be invisible inside map_other. */
    uint32_t iso_n     = pm_metal_py_isolated_ctx_count();
    uint64_t iso_bytes = (uint64_t)pm_metal_py_isolated_ctx_bytes();
    char     iob[PM_METAL_UTIL_SIZE_FORMAT_MAX];
    char     pct_iso[8];

    CoreFmtBytes(iob, sizeof(iob), iso_bytes);
    CoreFmtPct1(pct_iso, sizeof(pct_iso), iso_bytes, map_other);
    snprintf(line,
             sizeof(line),
             "  %s |   `-- py iso  %s   (%s of map, %u isolated ctx)",
             branch,
             iob,
             pct_iso,
             iso_n);
    pm_metal_shell_out(line);
  }
  snprintf(line,
           sizeof(line),
           "  %s +-- hole    %s   (%s of arena; free to grow map|heap)",
           branch,
           o,
           pct_hole);
  pm_metal_shell_out(line);
  snprintf(line,
           sizeof(line),
           "  %s `-- heap    %s   (%s of arena; %s used, %s free in TLSF)",
           branch,
           h,
           pct_heap,
           hu,
           pct_heap_free);
  pm_metal_shell_out(line);

  if (phys != 0) {
    snprintf(line, sizeof(line), "  `-- other   %s   (UEFI/firmware/reserved)", outb);
    pm_metal_shell_out(line);
  }
}

static const char *CorePsUiName(pm_metal_process_ui_kind_t kind)
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

static void CorePsCmd(int32_t argc, char **argv)
{
  pm_metal_process_info_t list[PM_METAL_PROCESS_MAX];
  uint32_t                n;
  uint32_t                i;
  char                    line[96];

  (void)argc;
  (void)argv;
  n = pm_metal_process_list(list, PM_METAL_PROCESS_MAX);
  if (n == 0) {
    pm_metal_shell_out("ps: no processes");
    return;
  }

  snprintf(line, sizeof(line), "ps: %u", n);
  pm_metal_shell_out(line);
  for (i = 0; i < n; i++) {
    snprintf(line,
             sizeof(line),
             "  %u %s ui=%s tab=%u surf=%u%s",
             (uint32_t)list[i].id,
             list[i].name,
             CorePsUiName((pm_metal_process_ui_kind_t)list[i].ui_kind),
             (uint32_t)list[i].tab,
             list[i].surface,
             (list[i].id == pm_metal_process_current()) ? " *" : "");
    pm_metal_shell_out(line);
  }
}

/**
 * Per-runner busy % + work-stealing diagnostics (docs/COOP_MEMORY.md:
 * N CPUs = N equal cooperative runners, lock-free ready rings — no CPU
 * affinity/pinning). `steals` = how many times another CPU claimed a
 * slot from this ring instead of its own owner; `retries` = lost claim
 * CAS races (contention, not correctness). While a guest session is
 * live, `pm_metal_async_session_cpu` is diagnostic-only: it marks the
 * runner that began the session, not a scheduling constraint — guest
 * tasks are free to run on any CPU, so other runners busy% is expected
 * to be > 0.
 */
static void CoreCpuCmd(int32_t argc, char **argv)
{
  uint32_t n_cpus;
  uint32_t i;
  uint32_t session_cpu;
  int32_t  has_session;
  char     line[128];

  (void)argc;
  (void)argv;

  n_cpus = pm_metal_mem_n_cpus();
  if (n_cpus == 0) {
    n_cpus = 1;
  }

  has_session = pm_metal_async_session_active();
  session_cpu = has_session ? (uint32_t)pm_metal_async_session_cpu() : 0;

  snprintf(line, sizeof(line), "cpu: %u runners", n_cpus);
  pm_metal_shell_out(line);
  for (i = 0; i < n_cpus; i++) {
    snprintf(line,
             sizeof(line),
             "  cpu%u  busy=%3u%%  steals=%u  retries=%u%s",
             i,
             pm_metal_run_busy_pct(i),
             pm_metal_run_steal_count(i),
             pm_metal_run_claim_retries(i),
             (has_session && i == session_cpu) ? "  <- session began here" : "");
    pm_metal_shell_out(line);
  }
}

/**
 * Diagnostic A/B switch: `presentoffload [on|off]` — forces the legacy
 * inline present path when off, so the cross-runner offload can be
 * compared against the old behavior without a rebuild (see async_ops.c).
 */
static void CorePresentOffloadCmd(int32_t argc, char **argv)
{
  char line[64];

  if (argc >= 2) {
    if (strcmp(argv[1], "off") == 0) {
      pm_metal_async_present_offload_set(0);
    } else if (strcmp(argv[1], "on") == 0) {
      pm_metal_async_present_offload_set(1);
    } else {
      pm_metal_shell_out("presentoffload: usage: presentoffload [on|off]");
      return;
    }
  }

  snprintf(
    line, sizeof(line), "presentoffload: %s", pm_metal_async_present_offload_get() ? "on" : "off");
  pm_metal_shell_out(line);
}

static void CoreHelpCmd(int32_t argc, char **argv)
{
  (void)argc;
  (void)argv;
  pm_metal_shell_cmd_help();
}

static void CoreDateCmd(int32_t argc, char **argv)
{
  uint64_t ms;
  uint32_t tod;
  uint32_t hour;
  uint32_t min;
  uint32_t sec;
  int32_t  tz;
  char     line[64];

  (void)argc;
  (void)argv;
  ms   = pm_metal_tz_local_ms();
  tod  = (uint32_t)((ms / 1000ull) % 86400ull);
  hour = tod / 3600u;
  min  = (tod % 3600u) / 60u;
  sec  = tod % 60u;
  tz   = pm_metal_tz_minutes();
  snprintf(line,
           sizeof(line),
           "%02u:%02u:%02u %s (UTC%c%02d%02d)",
           hour,
           min,
           sec,
           pm_metal_tz_name(),
           (tz < 0) ? '-' : '+',
           (tz < 0) ? (-tz) / 60 : tz / 60,
           (tz < 0) ? (-tz) % 60 : tz % 60);
  pm_metal_shell_out(line);
}

static void CoreTzCmd(int32_t argc, char **argv)
{
  char    line[80];
  int32_t tz;

  if (argc >= 2 && argv[1] != NULL && argv[1][0] != '\0') {
    if (pm_metal_tz_set(argv[1]) != 0) {
      pm_metal_shell_out("tz: unknown (use +HHMM or Europe/Berlin)");
      return;
    }
  }

  tz = pm_metal_tz_minutes();
  snprintf(line,
           sizeof(line),
           "tz %s (UTC%c%02d%02d)",
           pm_metal_tz_name(),
           (tz < 0) ? '-' : '+',
           (tz < 0) ? (-tz) / 60 : tz / 60,
           (tz < 0) ? (-tz) % 60 : tz % 60);
  pm_metal_shell_out(line);
}

/** Space-join argv[start..argc-1] into out (truncated to out_sz - 1), empty
 * string if there's nothing to join. Shared by echo (whole line) and
 * run/tab (trailing args after the mod name -> pm_metal_process_info_t.cmdline). */
static void CoreJoinArgs(char *out, uintptr_t out_sz, int32_t argc, char **argv, int32_t start)
{
  int32_t   i;
  uintptr_t off;
  uintptr_t n;

  if (out == NULL || out_sz == 0) {
    return;
  }

  off = 0;
  for (i = start; i < argc && argv[i] != NULL; i++) {
    n = strlen(argv[i]);
    if (off != 0) {
      if (off + 1 >= out_sz) {
        break;
      }

      out[off++] = ' ';
    }

    if (off + n >= out_sz) {
      n = out_sz - 1u - off;
    }

    memcpy(out + off, argv[i], n);
    off += n;
  }

  out[off] = '\0';
}

static void CoreEchoCmd(int32_t argc, char **argv)
{
  char line[160];

  if (argc < 2) {
    pm_metal_shell_out("");
    return;
  }

  CoreJoinArgs(line, sizeof(line), argc, argv, 1);
  pm_metal_shell_out(line);
}

static void CoreRunCmd(int32_t argc, char **argv)
{
  char args[128];

  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out("usage: run <mod> [args...]");
    return;
  }

  CoreJoinArgs(args, sizeof(args), argc, argv, 2);
  (void)pm_metal_shell_run_args(argv[1], args[0] != '\0' ? args : NULL);
}

static void CoreTabCmd(int32_t argc, char **argv)
{
  char args[128];

  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out("usage: tab <mod> [args...]");
    return;
  }

  CoreJoinArgs(args, sizeof(args), argc, argv, 2);
  (void)pm_metal_shell_tab_args(argv[1], args[0] != '\0' ? args : NULL);
}

static void CoreTabsCmd(int32_t argc, char **argv)
{
  uint32_t n;
  uint32_t i;
  uint32_t a;
  char     line[80];

  (void)argc;
  (void)argv;
  n = pm_metal_ui_tab_count();
  a = pm_metal_ui_tab_active_index();
  snprintf(line, sizeof(line), "tabs: %u  active: %u", n, a);
  pm_metal_shell_out(line);
  for (i = 0; i < n; i++) {
    snprintf(line, sizeof(line), "  %u%s", i, (i == a) ? " *" : "");
    pm_metal_shell_out(line);
  }
}

static void CoreUseCmd(int32_t argc, char **argv)
{
  uintptr_t i;

  if (argc < 2) {
    pm_metal_shell_out("usage: use <n>");
    return;
  }

  i = strtoul(argv[1], NULL, 10);
  if (pm_metal_ui_tab_activate_index((uint32_t)i) != 0) {
    pm_metal_shell_out("use: bad index");
  } else {
    char msg[40];

    snprintf(msg, sizeof(msg), "active tab %u", (uint32_t)i);
    pm_metal_ui_set_status(msg);
    pm_metal_shell_mark_full();
  }
}

static void CoreCloseCmd(int32_t argc, char **argv)
{
  uint32_t idx;
  uint32_t n;
  uint32_t a;

  n = pm_metal_ui_tab_count();
  a = pm_metal_ui_tab_active_index();
  if (argc >= 2) {
    idx = (uint32_t)strtoul(argv[1], NULL, 10);
  } else if (a != 0) {
    idx = a;
  } else if (n > 1) {
    idx = n - 1;
  } else {
    pm_metal_shell_out("close: no guest tab");
    return;
  }

  if (idx == 0) {
    pm_metal_shell_out("close: cannot close console");
  } else if (pm_metal_ui_tab_activate_index(idx) != 0 || pm_metal_ui_tab_close_active() != 0) {
    pm_metal_shell_out("close: failed");
  } else {
    (void)pm_metal_ui_tab_activate_index(0);
    pm_metal_ui_set_status("tab closed");
    pm_metal_shell_out("tab closed");
    pm_metal_shell_mark_full();
  }
}

static void CoreExitCmd(int32_t argc, char **argv)
{
  int32_t reboot;

  if (argc > 2) {
    pm_metal_shell_out("usage: exit [-r]");
    return;
  }

  if (argc == 2 && strcmp(argv[1], "-r") != 0) {
    pm_metal_shell_out("exit: use -r to reboot");
    return;
  }

  reboot = (argc == 2) ? 1 : 0;
  pm_metal_shell_out(reboot ? "reboot requested" : "shutdown requested");
  pm_metal_shell_cmd_exit(reboot);
}

static void CoreHistoryCmd(int32_t argc, char **argv)
{
  uint32_t n;
  uint32_t i;
  char     entry[128];
  char     line[144];

  (void)argc;
  (void)argv;
  n = pm_metal_shell_history_count();
  if (n == 0u) {
    pm_metal_shell_out("history: (empty)");
    return;
  }

  for (i = 0; i < n; i++) {
    if (pm_metal_shell_history_get(i, entry, sizeof(entry)) != 0) {
      continue;
    }

    snprintf(line, sizeof(line), "%4u  %s", i + 1u, entry);
    pm_metal_shell_out(line);
  }
}

static void CoreJobsCmd(int argc, char **argv)
{
  char line[96];

  (void)argc;
  (void)argv;
  if (pm_metal_shell_job_list(line, sizeof(line)) != 0) {
    return;
  }

  pm_metal_shell_out(line);
}

static void CoreFgCmd(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (pm_metal_shell_job_fg() != 0) {
    pm_metal_shell_out("fg: no job");
  }
}

static void CoreBgCmd(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (pm_metal_shell_job_bg() != 0) {
    pm_metal_shell_out("bg: no job");
  }
}

PM_METAL_SHELL_CMDS(g_pm_metal_shell_cmds_core) = {
  { "help", "this text", CoreHelpCmd },
  { "echo", "echo <text>       print text", CoreEchoCmd },
  { "date", "date              local wall clock", CoreDateCmd },
  { "tz", "tz [+HHMM|name]   get/set timezone", CoreTzCmd },
  { "history", "history           list command history", CoreHistoryCmd },
  { "jobs", "jobs              list shell async job", CoreJobsCmd },
  { "fg", "fg                resume job in foreground", CoreFgCmd },
  { "bg", "bg                resume job in background", CoreBgCmd },
  { "run", "run <mod>         fullscreen in console (guest HID)", CoreRunCmd },
  { "tab", "tab <mod>         windowed in a new tab (guest HID)", CoreTabCmd },
  { "ps", "ps                list fake processes", CorePsCmd },
  { "cpu", "cpu               per-runner busy % + session pin", CoreCpuCmd },
  { "presentoffload",
    "presentoffload [on|off]  A/B: cross-runner present offload",
    CorePresentOffloadCmd },
  { "mem", "mem               system RAM + arena layout", CoreMemCmd },
  { "tabs", "tabs              list tabs", CoreTabsCmd },
  { "use", "use <n>           activate tab index", CoreUseCmd },
  { "close", "close [n]         close tab n, or active/last guest", CoreCloseCmd },
  { "exit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "quit", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
  { "shutdown", "exit|quit [-r]    power off (or reboot with -r)", CoreExitCmd },
};
PM_METAL_SHELL_CMDS_END(g_pm_metal_shell_cmds_core);
