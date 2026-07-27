/** @file Shell py — spawn task / C→Py call on always-on blob. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pymergetic/metal/boot/externals.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/hwinfo/hwinfo.h>
#include <pymergetic/metal/util/ascii.h>
#include <pymergetic/metal/version.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>

static void PyShellUsage(void)
{
  /*
	 * Multi-line on purpose — flags will grow.
	 * -f is only the C→Py bind/call smoke path (demo modules like c_py_demo).
	 */
  pm_metal_shell_out("usage: py");
  pm_metal_shell_out("  py <script.py>                 run a script");
  pm_metal_shell_out("  py -c '<code>'                 run a one-liner");
  pm_metal_shell_out("  py -f <mod.fn> [ints...]       call a bound Python fn");
  pm_metal_shell_out("    py -f c_py_demo.add 2 3      -> prints 5");
  pm_metal_shell_out("    py -f c_py_demo.blink 50000  -> runs until blink finishes");
  pm_metal_shell_out("  py -x <script.py> | -x -c '<code>'");
  pm_metal_shell_out("    own VM context (own heap, no stdlib.zip) -- runs in");
  pm_metal_shell_out("    real parallel with the shared context on another CPU");
  pm_metal_shell_out("  py -i                          start the persistent Python REPL");
  pm_metal_shell_out("    (console() at the >>> prompt pauses it, back to this shell)");
}

/*
 * Shared by both callers -- boot_init.c's cold-boot landing and
 * PyShellRepl()'s "py -i" resume below -- via pm_metal_log() directly
 * (the same sink pm_metal_shell_out() mirrors into, see MetalShellEcho
 * in shell.c) so the two paths render identically and can't drift.
 * Feature-highlight lines are deliberately short and specific to what
 * this build actually does (grep the call site before changing a claim
 * here) -- the point is "what's different from stock Python", not a
 * generic feature list.
 */
void pm_metal_py_repl_print_banner(void)
{
  char                cpu_brand[64];
  char                line[128];
  pm_metal_external_t ext;
  uint32_t            i;
  uint32_t            n;

  pm_metal_util_ascii_log_rainbow("MetalPython");
  pm_metal_hwinfo_cpu_brand(cpu_brand, sizeof(cpu_brand));
  if (cpu_brand[0] != '\0') {
    pm_metal_logf("Metal %s  @  %s", PM_METAL_VERSION, cpu_brand);
  } else {
    pm_metal_logf("Metal %s", PM_METAL_VERSION);
  }

  n = pm_metal_external_count();
  for (i = 0u; i < n; i++) {
    if (pm_metal_external_get(i, &ext) != 0 || ext.id == NULL) {
      continue;
    }
    if (ext.version != NULL && ext.version[0] != '\0') {
      snprintf(line, sizeof(line), "  - %s %s", ext.id, ext.version);
    } else {
      snprintf(line, sizeof(line), "  - %s", ext.id);
    }
    pm_metal_logf("%s", line);
  }

  pm_metal_log("");
  pm_metal_log(
    "\033[1;35mMetal Python\033[0m -- persistent REPL, shared context, globals stick around.");
  pm_metal_log("Type console() at the >>> prompt to pause it and return to this shell.");
  pm_metal_log("");
  pm_metal_log("  - pymergetic.metal.* \033[2m<->\033[0m C: Python calls C, C calls back into "
               "Python -- one bind table, both directions");
  pm_metal_log(
    "  - Python task == Metal task: FCFS across every CPU runner, no GIL, no private Python loop");
  pm_metal_log("  - Real \033[1mawait\033[0m: Python coroutines and C coroutines share one "
               "scheduler (`await metal.aio.sleep_us(...)`)");
  pm_metal_log("  - `py -x`: opt-in isolated context, own heap + own GC -- genuine parallel "
               "bytecode on another core");
  pm_metal_log(
    "  - Signed wasm/AOT natives self-register straight into Python: `metal.mod.<name>.<fn>(...)`");
  pm_metal_log("");
}

static void PyShellRepl(void)
{
  pm_metal_async_handle_t task_h;

  if (pm_metal_py_repl_active()) {
    pm_metal_shell_out("py: repl already running -- type console() at >>> to pause it");
    return;
  }

  task_h = pm_metal_py_repl_start();
  if (task_h == 0) {
    pm_metal_shell_out("py: repl start failed");
    return;
  }

  pm_metal_py_repl_print_banner();
}

static int32_t PyParseI32(const char *s, int32_t *out)
{
  const char *p;
  int32_t     neg;
  uintptr_t   v;

  if (s == NULL || out == NULL || s[0] == '\0') {
    return -1;
  }

  p   = s;
  neg = 0;
  if (*p == '-') {
    neg = 1;
    p++;
    if (*p == '\0') {
      return -1;
    }
  }

  for (; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') {
      return -1;
    }
  }

  v    = strtoul(neg ? s + 1 : s, NULL, 10);
  *out = neg ? -(int32_t)v : (int32_t)v;
  return 0;
}

static void PyShellCall(int32_t argc, char **argv)
{
  pm_metal_py_fn_t        fn;
  pm_metal_async_handle_t task_h;
  uint64_t                deadline;
  int32_t                 a;
  int32_t                 b;
  int32_t                 sum;
  char                    line[80];
  const char             *name;
  int32_t                 narg;

  if (pm_metal_shell_job_busy()) {
    pm_metal_shell_out("py: busy");
    return;
  }

  if (argc < 3 || argv[2] == NULL || argv[2][0] == '\0') {
    PyShellUsage();
    return;
  }

  name = argv[2];
  narg = argc - 3;
  if (pm_metal_py_fn_bind(&fn, name) != 0) {
    pm_metal_shell_out("py: bind failed");
    return;
  }

  if (narg >= 2) {
    if (PyParseI32(argv[3], &a) != 0 || PyParseI32(argv[4], &b) != 0) {
      pm_metal_shell_out("py: bad args (want ints)");
      return;
    }

    sum = 0;
    if (pm_metal_py_call(&fn, &sum, a, b) != 0) {
      pm_metal_shell_out("py: call failed");
      return;
    }

    snprintf(line, sizeof(line), "py: %s=%d", name, sum);
    pm_metal_shell_out(line);
    return;
  }

  /* 0 or 1 int → async (arg0 defaults to 0). */
  a = 0;
  if (narg == 1) {
    if (PyParseI32(argv[3], &a) != 0) {
      pm_metal_shell_out("py: bad args (want int)");
      return;
    }
  }

  task_h = pm_metal_py_fn_call_async_bound(&fn, (uint32_t)a);
  if (task_h == 0) {
    pm_metal_shell_out("py: call_async failed");
    return;
  }

  deadline = pm_metal_time_mono_us() + 60000000ull;
  if (pm_metal_shell_job_start("py", task_h, 0, NULL, deadline) != 0) {
    pm_metal_async_task_cancel(task_h);
    pm_metal_shell_out("py: job failed");
    return;
  }

  pm_metal_shell_out("py: ...");
}

static void PyShellRun(int32_t argc, char **argv)
{
  pm_metal_async_handle_t task_h;
  uint64_t                deadline;
  const char             *src_or_path;
  int32_t                 is_cmd;
  int32_t                 is_isolated;
  int32_t                 argi;

  if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "-f") == 0) {
    PyShellCall(argc, argv);
    return;
  }

  if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "-i") == 0) {
    PyShellRepl();
    return;
  }

  if (pm_metal_shell_job_busy()) {
    pm_metal_shell_out("py: busy");
    return;
  }

  argi        = 1;
  is_isolated = (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "-x") == 0) ? 1 : 0;
  if (is_isolated) {
    argi = 2;
  }

  if (argc <= argi || argv[argi] == NULL || argv[argi][0] == '\0') {
    PyShellUsage();
    return;
  }

  is_cmd = (strcmp(argv[argi], "-c") == 0) ? 1 : 0;
  if (is_cmd) {
    if (argc <= argi + 1 || argv[argi + 1] == NULL || argv[argi + 1][0] == '\0') {
      PyShellUsage();
      return;
    }

    src_or_path = argv[argi + 1];
    task_h =
      is_isolated ? pm_metal_py_run_str_isolated(src_or_path, 0) : pm_metal_py_run_str(src_or_path);
  } else {
    src_or_path = argv[argi];
    task_h      = is_isolated ? pm_metal_py_run_script_isolated(src_or_path, 0)
                              : pm_metal_py_run_script(src_or_path);
  }

  if (task_h == 0) {
    pm_metal_shell_out("py: start failed");
    return;
  }

  deadline = pm_metal_time_mono_us() + 60000000ull;
  if (pm_metal_shell_job_start("py", task_h, 0, NULL, deadline) != 0) {
    pm_metal_async_task_cancel(task_h);
    pm_metal_shell_out("py: job failed");
    return;
  }

  pm_metal_shell_out("py: ...");
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_py,
                   "py",
                   "py [-x] <script>|-c <code> | -f <mod.fn> [args] | -i",
                   PyShellRun);
