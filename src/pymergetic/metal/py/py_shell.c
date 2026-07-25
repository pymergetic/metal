/** @file Shell py — spawn task / C→Py call on always-on blob. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
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

  if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "-f") == 0) {
    PyShellCall(argc, argv);
    return;
  }

  if (pm_metal_shell_job_busy()) {
    pm_metal_shell_out("py: busy");
    return;
  }

  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    PyShellUsage();
    return;
  }

  is_cmd = (strcmp(argv[1], "-c") == 0) ? 1 : 0;
  if (is_cmd) {
    if (argc < 3 || argv[2] == NULL || argv[2][0] == '\0') {
      PyShellUsage();
      return;
    }

    src_or_path = argv[2];
    task_h      = pm_metal_py_run_str(src_or_path);
  } else {
    src_or_path = argv[1];
    task_h      = pm_metal_py_run_script(src_or_path);
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
                   "py <script> | -c <code> | -f <mod.fn> [args]",
                   PyShellRun);
